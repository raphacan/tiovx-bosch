/*
 * Copyright (c) 2024 The Khronos Group Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <vx_internal.h>

static vx_int32 send_buffer_release_message(vx_consumer consumer, void* message_buffer, vx_uint8 buffer_id, message_type_e message_type)
{
    vx_int32 status = 0U;
    consumer_msg_content_t* msg   = (consumer_msg_content_t*)message_buffer;
    msg->msg_type           = message_type;
    msg->consumer_id        = consumer->cons_base.consumer_id;
    msg->buffer_id          = buffer_id;

    // check if this is the last buffer which has been received - either dequeued or dropped
    if ((consumer->cons_base.last_buffer == 1) &&
        ((consumer->cons_base.last_buffer_id == buffer_id) || (consumer->cons_base.last_buffer_dropped == 1)))
    {
        consumer->cons_base.state = VX_CONS_STATE_WAIT;
        VX_PRINT(
            VX_ZONE_INFO,
            "CONSUMER: last buffer has been processed, wait before putting pipeline in flush mode%s",
            "\n");
        msg->last_buffer = 1U;
    }

    status = ippc_shem_send(&consumer->m_sender_ctx);
    if (status < 0)
    {
        VX_PRINT(VX_ZONE_ERROR, "CONSUMER: buffer ID %d message could not be sent%s", buffer_id, "\n");
    }

    return status;
}

static vx_status
import_ref_from_producer(vx_consumer consumer, producer_msg_content_t* buff_desc_msg)
{
    vx_status  status = VX_SUCCESS;
    consumer->cons_base.num_refs = buff_desc_msg->num_refs;

    if(0U == consumer->cons_base.num_refs)
    {
        status = (vx_status)VX_FAILURE;
    }

    for(vx_uint32 idx = 0U; idx < buff_desc_msg->num_refs; idx++)
    {
        // number of items message field must be set if references sent are members of an object array
        if (buff_desc_msg->buffer_info.num_items > 0)
        {
            for(vx_uint32 jdx = 0U; jdx < (buff_desc_msg->buffer_info.num_items + 1U); jdx++)
            {
                tivx_utils_ref_ipc_msg_t* ref_export_handle          = &buff_desc_msg->ref_export_handle[idx][jdx];
                // determine if receiving object array metadata (last message after object array items)
                if (ref_export_handle->refDesc.meta.type == VX_TYPE_OBJECT_ARRAY)
                {
                    VX_PRINT(
                        VX_ZONE_INFO,
                        "CONSUMER: Importing object array with %d, items of type %d\n",
                        buff_desc_msg->buffer_info.num_items,
                        ref_export_handle->refDesc.meta.type);

                    // finish reception of object array
                    vx_reference objarray_ref = NULL;
                    status = vx_utils_import_ref_from_ipc_xfer_objarray(
                        consumer->cons_base.context, ref_export_handle, (tivx_utils_ref_ipc_msg_t*)&consumer->cons_base.ipcMessageArray, &objarray_ref);
                    if ((status != VX_SUCCESS) && (vxGetStatus(objarray_ref) != VX_SUCCESS))
                    {
                        VX_PRINT(
                            VX_ZONE_ERROR,
                            "CONSUMER: vx_utils_import_ref_from_ipc_xfer_objarray() failed for ref [%d]\n",
                            consumer->cons_base.num_refs);
                    }
                    else
                    {
                        consumer->cons_base.refs[idx] = objarray_ref;
                        consumer->cons_base.ipcMessageCount = 0; // Wrap the intermediate reference counter
                    }
                }
                else
                {
                    // store object arra item metadata
                    VX_PRINT(
                        VX_ZONE_INFO,
                        "CONSUMER: Receiving object array item %d, of type %d\n",
                        jdx,
                        ref_export_handle->refDesc.meta.type);

                    for (vx_uint32 i = 0; i < ref_export_handle->numFd; i++)
                    {
                        consumer->cons_base.ipcMessageArray[consumer->cons_base.ipcMessageCount].fd[i] = ref_export_handle->fd[i];
                    }
                    consumer->cons_base.ipcMessageArray[consumer->cons_base.ipcMessageCount].refDesc = ref_export_handle->refDesc;
                    consumer->cons_base.ipcMessageArray[consumer->cons_base.ipcMessageCount].numFd = ref_export_handle->numFd;
                    consumer->cons_base.ipcMessageCount++;
                }
            }
        }
        else if (buff_desc_msg->buffer_info.num_items == 0)
        {
            VX_PRINT(VX_ZONE_INFO, "CONSUMER: Importing single reference of type %d\n", buff_desc_msg->ref_export_handle[idx][0].refDesc.meta.type);

            // receiving non-object array single reference
            vx_reference single_ref = NULL;
            status = tivx_utils_import_ref_from_ipc_xfer(consumer->cons_base.context, &buff_desc_msg->ref_export_handle[idx][0], &single_ref);
            if ((status == VX_SUCCESS) && (vxGetStatus(single_ref) == VX_SUCCESS))
            {
                consumer->cons_base.refs[consumer->cons_base.num_refs] = single_ref;
            }
            else
            {
                VX_PRINT(
                    VX_ZONE_ERROR,
                    "CONSUMER: tivx_utils_import_ref_from_ipc_xfer() failed for ref [%d]\n",
                    consumer->cons_base.num_refs);
            }
        }
        else
        {
            status = (vx_status)VX_FAILURE;
        }
    }

    return status;
}

void *consumer_backchannel(void* arg)
{
    vx_consumer consumer = (vx_consumer) arg;
    vx_reference dequeued_refs[VX_GC_MAX_NUM_REFS] = {0};

    char threadname[280U];
    snprintf(threadname, 280U, "%s_bck_thread", consumer->cons_base.name);
    pthread_setname_np(pthread_self(), threadname);

    VX_PRINT(VX_ZONE_INFO, "CONSUMER: Starting backchannel %s", "\n");

    if (NULL == consumer)
    {
        VX_PRINT(VX_ZONE_ERROR, "CONSUMER: Bad argument, shutting down thread%s", "\n");
        return NULL;
    }    

    do
    {
        vx_uint32 num_dequeued_refs = 0U;
        vx_status status = consumer->cons_base.dequeue_callback(consumer->cons_base.graph_obj, dequeued_refs, &num_dequeued_refs);
        if (status != VX_SUCCESS)
        {
            VX_PRINT(VX_ZONE_ERROR, "CONSUMER: Error while dequeuing buffer %s", "\n");
        }
        for (vx_uint32 current_ref_num = 0; current_ref_num < num_dequeued_refs; current_ref_num++)
        {
            // Process one reference at a time
            vx_reference ref_from_graph = dequeued_refs[current_ref_num];
            //  Get producer internal buffer id, and send it to the consumer
            vx_int32 buffer_id = getBufferIdForConsumer(ref_from_graph, consumer->cons_base.refs, consumer->cons_base.num_refs);
            VX_PRINT(
                    VX_ZONE_INFO,
                    "CONSUMER: dequeue successfull, send the buffer id: %d back to producer!\n",
                    buffer_id);
            if (buffer_id < 0)
            {
                VX_PRINT(VX_ZONE_ERROR, "CONSUMER: wrong buffer ID %d \n", buffer_id);
                consumer->cons_base.state = VX_CONS_STATE_FAILED;
                break;
            }
            else
            {
                // we have something dequeued and the graph is finished processing
                EIppcStatus ippc_status;
                consumer_msg_content_t* l_send_msg;
                pthread_mutex_lock(&consumer->cons_base.buffer_mutex);
                l_send_msg = ippc_shem_payload_pointer(&consumer->m_sender_ctx, sizeof(consumer_msg_content_t), &ippc_status);
                if (ippc_status == E_IPPC_OK)
                {
                    VX_PRINT(
                        VX_ZONE_INFO,
                        "CONSUMER: current buffer ID %d last buffer ID %d dequeued, last buffer flag %d \n",
                        buffer_id,
                        consumer->cons_base.last_buffer_id,
                        consumer->cons_base.last_buffer);
                    l_send_msg->last_buffer = consumer->cons_base.last_buffer;
                    send_buffer_release_message(consumer, l_send_msg, buffer_id, VX_MSGTYPE_BUF_RELEASE);
                }
                pthread_mutex_unlock(&consumer->cons_base.buffer_mutex);
            }
        }
    } while ((0U == consumer->cons_base.last_buffer) && (VX_CONS_STATE_FLUSH != consumer->cons_base.state));
    return NULL;
}

void handle_receive_message(const void * consumer, const void * data, vx_uint8 last_buffer_from_series)
{
    vx_int32  status = 0;
    EIppcStatus l_status = (EIppcStatus)E_IPPC_OK;
    vx_consumer cons = (vx_consumer) consumer;
    producer_msg_content_t* const l_received_message = (producer_msg_content_t*)data;
    
    //if the consumer is ready to communicate, send the back channel port id to the producer
    if ((vx_bool)vx_false_e == cons->cons_base.init_done)
    {
        VX_PRINT(VX_ZONE_INFO, "CONSUMER %u (%s): attaching to backhannel \n", cons->cons_base.consumer_id, cons->cons_base.name);
        /* feed the data for the sender */
        cons->m_sender_ctx.m_msg_size = sizeof(consumer_msg_content_t);
        const SIppcPortMap *l_port = ippc_get_port_by_recv_index(cons->ippc_port, cons->cons_base.consumer_id);
        cons->m_sender_ctx.m_port_map.m_port_id        = l_port->m_port_id;
        cons->m_sender_ctx.m_port_map.m_port_type      = l_port->m_port_type;
        cons->m_sender_ctx.m_port_map.m_receiver_index = l_port->m_receiver_index;

        l_status = ippc_registry_sender_attach(&cons->m_registry, &cons->m_sender_ctx.m_sender, 
                                                cons->m_sender_ctx.m_port_map.m_port_id, cons->m_sender_ctx.m_msg_size);

        if (E_IPPC_OK == l_status)
        {
            // sender is on 1->1 port, attach to a single sync; offset by number of syncs for broadcast
            l_status = ippc_registry_sync_attach(&cons->m_registry, &cons->m_sender_ctx.m_sync[0], 
                                                    cons->m_sender_ctx.m_port_map.m_receiver_index + VX_GC_NUM_CLIENTS);
        }

        if (E_IPPC_OK != l_status)
        {
            status = VX_FAILURE;
            VX_PRINT(VX_ZONE_ERROR, "CONSUMER: Failed to attach to back channel port!%s", "\n");
        }
        else
        {
            cons->cons_base.init_done = (vx_bool)vx_true_e;
        }
    }

    // For multiple client scenario, the import of references doesnt happen in first receive
    // Hence, wait until the data is available, then proceed
    if (((vx_bool)vx_true_e == cons->cons_base.init_done) && ((vx_bool)vx_false_e == cons->cons_base.ref_import_done))
    {
        status = import_ref_from_producer(cons, l_received_message);

        if (VX_SUCCESS == status)
        {
            status = cons->cons_base.create_graph_callback(cons->cons_base.graph_obj, cons->cons_base.refs, cons->cons_base.num_refs);
            if (status != VX_SUCCESS)
            {
                VX_PRINT(VX_ZONE_ERROR, "CONSUMER (%s): application create graph failed \n", cons->cons_base.name);
            }
            else
            {
                status = VX_CONS_STATUS_GRAPH_READY; 
                VX_PRINT(VX_ZONE_INFO, "CONSUMER (%s): application create graph success", cons->cons_base.name);
                // open a new backchannel thread to dequeue stuff
                int thread_status = pthread_create(&cons->cons_base.backchannel_thread, NULL, consumer_backchannel, (void*)(cons));
                if (thread_status != 0)
                {
                    VX_PRINT(VX_ZONE_ERROR, "CONSUMER (%s): failed to create consumer_backchannel client thread", cons->cons_base.name);
                }
                else
                {
                    cons->cons_base.ref_import_done = (vx_bool)vx_true_e;
                }
            }
        }
    }

    if (-1 != l_received_message->buffer_info.id)
    {
        if (1U == l_received_message->buffer_info.last_buffer)
        {
            // if last buffer was sent, producer needs to be notified by setting last buffer flag, response needs to be sent regardless of whether receiver was addressed via mask or not
            cons->cons_base.last_buffer = 1U;
            cons->cons_base.last_buffer_transmitted = 1;
            cons->cons_base.last_buffer_id = l_received_message->buffer_info.id;
            VX_PRINT(VX_ZONE_INFO, "CONSUMER (%s): Received last buffer id %d \n", cons->cons_base.name, cons->cons_base.last_buffer_id);
        }            

        if (l_received_message->buffer_info.mask & (1U << cons->cons_base.consumer_id)) // mask applies to consumer
        {
            if((1U == last_buffer_from_series)) // pass reference to graph
            {
                VX_PRINT(
                    VX_ZONE_INFO,
                    "CONSUMER (%s): Received buffer ID (%d), last buffer %d mask %d\n",
                    cons->cons_base.name,
                    l_received_message->buffer_info.id,
                    l_received_message->buffer_info.last_buffer, l_received_message->buffer_info.mask);
                // check if metadata from IPC was received and apply it to consumer reference
                if ((1U == l_received_message->buffer_info.metadata.is_valid) && (NULL != cons->cons_base.receive_metadata_callback))
                {
                    status = cons->cons_base.receive_metadata_callback(
                        cons->cons_base.graph_obj, cons->cons_base.refs[l_received_message->buffer_info.id], &l_received_message->metadata_buffer, l_received_message->buffer_info.metadata.size);
                }

                VX_PRINT(
                    VX_ZONE_INFO,
                    "CONSUMER (%s): enqueue the incoming buffer id: %d with ref: %p into the pipeline as input buffer, last "
                    "buffer %d mask %d \n",
                    cons->cons_base.name,
                    l_received_message->buffer_info.id,
                    cons->cons_base.refs[l_received_message->buffer_info.id],
                    l_received_message->buffer_info.last_buffer, 
                    l_received_message->buffer_info.mask);

                status = cons->cons_base.enqueue_callback(cons->cons_base.graph_obj, (vx_reference)cons->cons_base.refs[l_received_message->buffer_info.id]);            
            }
            else // drop reference   
            {
                VX_PRINT(VX_ZONE_INFO, "CONSUMER (%s): Received buffer ID (%d) mask %d set for this consumer, but not latest message, drop ref \n", 
                    cons->cons_base.name, l_received_message->buffer_info.id, l_received_message->buffer_info.mask);
                status = VX_CONS_STATUS_REF_DROP;
            }  
        }
        else // mask not set, respond to producer only if it was last buffer sent by producer
        {
            VX_PRINT(VX_ZONE_INFO, "CONSUMER (%s): Received buffer ID (%d) mask %d not set for this consumer, return success without notifying Producer\n", cons->cons_base.name, l_received_message->buffer_info.id, l_received_message->buffer_info.mask);
            if(1U == l_received_message->buffer_info.last_buffer) // only in case of last buffer, notify producer
            {
                status = VX_CONS_STATUS_REF_DROP;
            }
        } 
    }

    if (0 > status)
    {
        VX_PRINT(VX_ZONE_ERROR, "CONSUMER (%s): MSG RECEIVE STATUS: FAILED.", cons->cons_base.name);
    }
    else if (cons->cons_base.state == VX_CONS_STATE_FLUSH)
    {
        // buffer was not enqueued, transmit the buffer back to the producer immediately and shutdown
        cons->cons_base.last_buffer_dropped = 1U;

        consumer_msg_content_t* l_send_msg;
        pthread_mutex_lock(&cons->cons_base.buffer_mutex);
        l_send_msg = ippc_shem_payload_pointer(&cons->m_sender_ctx, sizeof(consumer_msg_content_t), &l_status);
        l_send_msg->last_buffer = 1U;
        if(E_IPPC_OK == l_status)
        {
            VX_PRINT(VX_ZONE_INFO, "CONSUMER (%s): CONSUMER DROPS FRAME and sets last buffer flag, BUFFER ID %d, %s.", cons->cons_base.name, l_received_message->buffer_info.id, "\n");
            send_buffer_release_message(cons, l_send_msg, l_received_message->buffer_info.id, VX_MSGTYPE_BUF_RELEASE);
        }
        pthread_mutex_unlock(&cons->cons_base.buffer_mutex);
    }
    else if (VX_CONS_STATUS_REF_DROP == status)
    {
        // buffer was not enqueued, transmit the buffer back to the producer immediately
        cons->cons_base.last_buffer_dropped = 1U;

        consumer_msg_content_t* l_send_msg;
        pthread_mutex_lock(&cons->cons_base.buffer_mutex);
        l_send_msg = ippc_shem_payload_pointer(&cons->m_sender_ctx, sizeof(consumer_msg_content_t), &l_status);
        l_send_msg->last_buffer = 0U;
        if(E_IPPC_OK == l_status)
        {
            VX_PRINT(VX_ZONE_INFO, "CONSUMER (%s): CONSUMER DROPS FRAME, BUFFER ID %d, %s.", cons->cons_base.name, l_received_message->buffer_info.id, "\n");
            send_buffer_release_message(cons, l_send_msg, l_received_message->buffer_info.id, VX_MSGTYPE_BUF_RELEASE);
        }
        pthread_mutex_unlock(&cons->cons_base.buffer_mutex);
    }
    else if (VX_CONS_STATUS_GRAPH_READY == status)
    {
        // notify producer that consumer is ready to consume 
        consumer_msg_content_t* l_send_msg;
        pthread_mutex_lock(&cons->cons_base.buffer_mutex);
        l_send_msg = ippc_shem_payload_pointer(&cons->m_sender_ctx, sizeof(consumer_msg_content_t), &l_status);
        l_send_msg->last_buffer = 0U;
        if(E_IPPC_OK == l_status)
        {
            VX_PRINT(VX_ZONE_INFO, "CONSUMER (%s): send graph ready to producer, BUFFER ID %d, %s.", cons->cons_base.name, l_received_message->buffer_info.id, "\n");
            send_buffer_release_message(cons, l_send_msg, l_received_message->buffer_info.id, VX_MSGTYPE_CONSUMER_CREATE_DONE);
        }
        pthread_mutex_unlock(&cons->cons_base.buffer_mutex);
    }
    else
    {
        cons->cons_base.last_buffer_dropped = 0;
        status                        = 0;
    }
    
}

static void* consumerReceiverThread(void* arg)
{
    vx_consumer consumer = (vx_consumer)arg;
    vx_bool shutdown = (vx_bool)vx_false_e;
    EIppcStatus status = (EIppcStatus)E_IPPC_OK;

    char threadname[280U];
    snprintf(threadname, 280U, "%s_receiver_thread", consumer->cons_base.name);
    pthread_setname_np(pthread_self(), threadname);

    while((vx_bool)vx_false_e == shutdown)
    {
        switch(consumer->cons_base.state)
        {
            case VX_CONS_STATE_DISCONNECTED:
            {
                status = ippc_shm_exists(consumer->cons_base.access_point_name);
                if (E_IPPC_OK == status)
                {
                    status = ippc_shm_attach_registry(&consumer->m_registry, consumer->cons_base.access_point_name);

                }
                
                if (E_IPPC_OK == status)
                {
                    VX_PRINT(VX_ZONE_INFO, "CONSUMER: connection made with producer on SHM %s\n", consumer->cons_base.access_point_name);
                    consumer->cons_base.state = VX_CONS_STATE_INIT;
                }
                else
                {
                    VX_PRINT(VX_ZONE_INFO, "CONSUMER: Waiting for connection with producer...%s", "\n");
                    tivxTaskWaitMsecs(consumer->cons_base.connect_polling_time);
                }
            }
            break;

            case VX_CONS_STATE_INIT:
            {
                // attaching reciever now
                consumer->m_receiver_ctx.m_application_ctx = consumer;
                consumer->m_receiver_ctx.m_client_handler = handle_receive_message;

                /* feed the same information again into the receiver member, cleanup necessary */
                consumer->m_receiver_ctx.m_port_map.m_receiver_index = consumer->cons_base.consumer_id;
                consumer->m_receiver_ctx.m_port_map.m_port_id        = consumer->ippc_port[0].m_port_id;
                consumer->m_receiver_ctx.m_port_map.m_port_type      = consumer->ippc_port[0].m_port_type;
                /* we should avoid this kind of thing: consumer->m_receiver_ctx.m_receiver_ctx*/
                consumer->m_receiver_ctx.m_msg_size = sizeof(producer_msg_content_t);
                status  = ippc_registry_receiver_attach(&consumer->m_registry,
                                                        &consumer->m_receiver_ctx.m_receiver,
                                                        consumer->m_receiver_ctx.m_port_map.m_port_id,
                                                        consumer->m_receiver_ctx.m_port_map.m_receiver_index,
                                                        consumer->m_receiver_ctx.m_msg_size,
                                                        E_IPPC_RECEIVER_DISCARD_PAST);
                if (E_IPPC_OK == status)
                {
                    /* init the receiver */
                    status = ippc_registry_sync_attach(&consumer->m_registry, 
                                                        &consumer->m_receiver_ctx.m_sync, 
                                                        consumer->m_receiver_ctx.m_port_map.m_receiver_index);
                }

                if (E_IPPC_OK == status)
                {
                    VX_PRINT(VX_ZONE_INFO, " [UPT] First Time Connected to producer!%s", "\n");
                    VX_PRINT(VX_ZONE_INFO, "CONSUMER: attached to producer with SHM %s\n", consumer->cons_base.access_point_name);
                    consumer->cons_base.state = VX_CONS_STATE_RUN;
                }
                else
                {
                    VX_PRINT(VX_ZONE_ERROR, "CONSUMER: Could not attach with producer!%s", "\n");
                    consumer->cons_base.state = VX_CONS_STATE_FAILED;
                }                                                                                                
            }
            break;

            case VX_CONS_STATE_RUN:
            {
                if (consumer->cons_base.last_buffer)
                {
                    consumer->cons_base.state = VX_CONS_STATE_WAIT;
                }
                else
                {
                    ippc_receive(&consumer->m_receiver_ctx);
                }
            }
            break;

            case VX_CONS_STATE_WAIT:
            {
                tivxTaskWaitMsecs(100);
                VX_PRINT(VX_ZONE_INFO, "CONSUMER: going to flush state%s", "\n");
                consumer->cons_base.state = VX_CONS_STATE_FLUSH;
            }
            break;

            case VX_CONS_STATE_FAILED:
            {
                // consumer failed too many times OR a recovery cannot be done
                if (consumer->cons_base.num_failures >= CONSUMER_MAX_CONSECUTIVE_FAILURES || consumer->cons_base.recovery_callback == NULL ||
                    consumer->cons_base.last_buffer == 1U)
                {
                    VX_PRINT(VX_ZONE_ERROR, "CONSUMER: Disconnected too many times, shutting down %s", "\n");
                    consumer->cons_base.state = VX_CONS_STATE_FLUSH;
                    break;
                }

                consumer->cons_base.num_failures++;

                if (consumer->cons_base.recovery_callback != NULL)
                {
                    consumer->cons_base.num_refs        = 0;
                    consumer->cons_base.ipcMessageCount = 0;

                    // reset state machine
                    consumer->cons_base.state = VX_CONS_STATE_DISCONNECTED;

                    VX_PRINT(VX_ZONE_INFO, "CONSUMER: calling recovery callback %s", "\n");
                    consumer->cons_base.recovery_callback(consumer->cons_base.graph_obj);
                    // consumer deinit can be called from recovery callback, which will cause this thread to shut down
                }
            }
            break;

            case VX_CONS_STATE_FLUSH:
            {
                VX_PRINT(VX_ZONE_INFO, "CONSUMER: pipeline is flushed, reached normal shutdown%s", "\n");
                shutdown = (vx_bool)vx_true_e;
            }
            break;
        }
    }
    return NULL;
}

vx_status ownInitConsumerObjectIppc(vx_consumer consumer, const vx_consumer_params_t* params)
{
    vx_status status = (vx_status)VX_SUCCESS;
    for(vx_uint32 idx = 0U; idx < IPPC_PORT_COUNT; idx++)
    {
        consumer->ippc_port[idx] = params->gc_params->ippc_port[idx];
    }
    return status;
}

VX_API_ENTRY vx_status VX_API_CALL consumerStartIppc(vx_consumer consumer)
{
    /* start the ippc broadcasting thread */
    int thread_status = pthread_create(&consumer->cons_base.receiver_thread, NULL, consumerReceiverThread, (void*)consumer);
    return ((vx_status)thread_status);
}

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

static vx_int32 send_id_message_consumers( vx_producer producer, producer_msg_content_t* msg, vx_int32 buff_id);
static void fill_reference_info(vx_producer producer, producer_msg_content_t* buffid_message);
static vx_bool check_ippc_clients_connected(vx_producer producer);
static void producer_msg_handler(const void * producer_p, const void * data_p, vx_uint8 last_buffer_from_series);
static void* producer_bck_thread(void* arg);
static void* producer_broadcast_thread(void* arg);
static void* producer_connection_check_thread(void* arg);


static vx_int32 send_id_message_consumers(
                                            vx_producer producer,
                                            producer_msg_content_t* msg,
                                            vx_int32 buff_id)
{
    vx_int32 status = 0;
    vx_int32 sent_to_consumer = 0;
    vx_uint32 locked_cnt = 0U;
    vx_uint32 mask = 0U;
    for (vx_uint32 i = 0; i < VX_GC_NUM_CLIENTS; i++)
    {          
        if (
            (producer->consumers_list[i].state == PROD_STATE_CLI_GRAPH_VERIFIED)
        )
        {
            locked_cnt = getNumLockedFramesByClient(producer, i);
            if ((locked_cnt < producer->prod_base.max_refs_locked_by_client) || (1U == producer->prod_base.last_buffer)) // in case of last buffer, transmission of that buffer is still necessary
            {
                mask |= (1U << i);
                if (buff_id != -1)
                {
                    // the position of this locking is critical, the refcount should be incremented based on the number of
                    // successfull sends
                    status = setBufferStatus(buff_id, LOCKED, producer);
                    if (status != VX_SUCCESS)
                    {
                        VX_PRINT(
                            VX_ZONE_ERROR, "PRODUCER %s: Reference %u, could not be set to LOCKED \n", producer->prod_base.name, i);
                        break;
                    }

                    producer->prod_base.refs[buff_id].attached_to_client[i] = 1;
                }              
            }
        }
    }

    // in case mask is 0 (all consumers lock maximum amount of buffers allowed), 
    // free reference from here so producer can find it as an available ref
    if ((0U == mask) && (buff_id != -1))
    {
        setBufferStatus(buff_id, FREE, producer);
    }
    if (msg != NULL)
    {
        msg->buffer_info.mask = mask;
    }   

    status = ippc_shem_send(&producer->m_sender_ctx);
    if (status == E_IPPC_OK)
    {
        sent_to_consumer = VX_GC_NUM_CLIENTS;
        VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: buffer ID sent to consumers with mask %d\n", producer->prod_base.name, mask);
    }
    else
    {
        VX_PRINT(VX_ZONE_ERROR, "PRODUCER %s: buffer ID could not be sent to consumers with mask %d \n", producer->prod_base.name, mask);
    }

    return sent_to_consumer;
}

static void* producer_bck_thread(void* arg)
{
    producer_bckchannel_t* l_consumer = (producer_bckchannel_t*) arg;

    char threadname[280U];
    snprintf(threadname, 280U, "producer_bck_thread_%u", l_consumer->consumer_id);
    pthread_setname_np(pthread_self(), threadname);

    VX_PRINT(VX_ZONE_INFO, "PRODUCER : starting backchannel worker for consumer %u on port %u\n", l_consumer->consumer_id, 
                                                                    l_consumer->m_receiver_ctx.m_port_map.m_port_id);
    while(1)
    {
        // wait for message on backchannel
        ippc_receive(&l_consumer->m_receiver_ctx);
    }

    return NULL;
}

static void fill_reference_info(vx_producer producer, producer_msg_content_t* buffid_message)
{
    buffid_message->num_refs = producer->prod_base.num_buffer_refs_export;
    vx_uint32 i = 0;
    vx_uint32 j = 0;
    for (i = 0; i < producer->prod_base.num_buffer_refs_export; i++)
    {
        vx_enum                  ref_type;
        vx_uint32                num_items          = 0;
        tivx_utils_ref_ipc_msg_t ipc_message_parent = {0};
        tivx_utils_ref_ipc_msg_t ipc_message_item[VX_GC_MAX_NUM_REFS];

        vx_status framework_status =
            vxQueryReference(producer->prod_base.refs[i].ovx_ref, VX_REFERENCE_TYPE, (void*)&ref_type, (vx_size)sizeof(ref_type));
        if (framework_status != VX_SUCCESS)
        {
            VX_PRINT(VX_ZONE_ERROR, "PRODUCER: vxQueryReference() failed for object [%d]\n", i);
            break;
        }
        else if (ref_type == VX_TYPE_OBJECT_ARRAY)
        {
            framework_status = rbvx_utils_export_ref_for_ipc_xfer_objarray(
                producer->prod_base.refs[i].ovx_ref,
                &num_items,
                &ipc_message_parent,
                (tivx_utils_ref_ipc_msg_t*)&ipc_message_item[0]);

            buffid_message->buffer_info.num_items = num_items;

            // send object array items data, if present
            for (j = 0; j < num_items; j++)
            {
                buffid_message->ref_export_handle[i][j] = ipc_message_item[j];

                VX_PRINT(
                    VX_ZONE_INFO,
                    "PRODUCER %s: sending objarray element %d with fd count %d\n",
                    producer->prod_base.name,
                    j,
                    buffid_message->ref_export_handle[i][j].numFd);
            }
        }
        else
        {
            framework_status = tivx_utils_export_ref_for_ipc_xfer(producer->prod_base.refs[i].ovx_ref, &ipc_message_parent);
        }

        if (framework_status != VX_SUCCESS)
        {
            VX_PRINT(VX_ZONE_ERROR, "PRODUCER: export_ref_for_ipc_xfer() failed for buffer [%d]\n", i);
            break;
        }
        else
        {
            VX_PRINT(
                VX_ZONE_INFO,
                "PRODUCER %s: export of buffer successfull: %d of total: %d\n",
                producer->prod_base.name,
                i + 1,
                producer->prod_base.num_buffer_refs_export);
        }

        // send reference data, for object array this is final metadata
        buffid_message->ref_export_handle[i][j] = ipc_message_parent;
    }

}

static void producer_msg_handler(const void * producer_p, const void * data_p, vx_uint8 last_buffer_from_series)
{
    const consumer_msg_content_t* const received_msg = (const consumer_msg_content_t*)data_p;
    vx_producer producer = (vx_producer)producer_p;
    switch (received_msg->msg_type)
    {
        case VX_MSGTYPE_HELLO:
        case VX_MSGTYPE_REF_BUF:
        case VX_MSGTYPE_BUFID_CMD:
        case VX_MSGTYPE_COUNT:
        {
            // do nothing
        }
        break; 

        case VX_MSGTYPE_BUF_RELEASE:
        {
            if (received_msg->last_buffer == 1)
            {
                VX_PRINT(VX_ZONE_INFO, "received last_buffer release from consumer %d \n", received_msg->consumer_id); 
                // release all buffers in possession of this consumer
                for (vx_uint32 buffId = 0; buffId < producer->prod_base.num_buffers; buffId++)
                {
                    if (1U == producer->prod_base.refs[buffId].attached_to_client[received_msg->consumer_id])
                    {
                        producer->prod_base.refs[buffId].attached_to_client[received_msg->consumer_id] = 0U;
                        setBufferStatus(buffId, FREE, producer);
                    }
                } 
                producer->consumers_list[received_msg->consumer_id].state = PROD_STATE_CLI_FLUSHED;
            }
            else
            {
                VX_PRINT(
                    VX_ZONE_INFO,
                    "PRODUCER %s:Received release id: %d from consumer %d \n",
                    producer->prod_base.name,
                    received_msg->buffer_id,
                    received_msg->consumer_id);
                vx_reference next_out_ref = producer->prod_base.refs[received_msg->buffer_id].ovx_ref;
                if (next_out_ref != NULL)
                {
                    // enqueue the new buffer in the handle producer thread, here the refcount is decreased
                    producer->prod_base.refs[received_msg->buffer_id].attached_to_client[received_msg->consumer_id] = 0;
                    setBufferStatus(received_msg->buffer_id, FREE, producer);
                }
                else
                {
                    VX_PRINT(VX_ZONE_ERROR, "PRODUCER %s: buffer ID not valid.\n", producer->prod_base.name);
                }
            }
        }
        break;

        case VX_MSGTYPE_CONSUMER_CREATE_DONE: // consumer notifys about graph creation being completed
        {
            VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: received VX_GC_STATUS_CONSUMER_CREATE_DONE state from consumer %d \n", producer->prod_base.name, received_msg->consumer_id);
            producer->consumers_list[received_msg->consumer_id].state = PROD_STATE_CLI_GRAPH_VERIFIED; 
        }
        break;

        default:
            VX_PRINT(
                VX_ZONE_ERROR,
                "PRODUCER %s: Received [UNKNOWN MESSAGE] %d\n",
                producer->prod_base.name,
                received_msg->msg_type);
        break;
    }
}

static vx_bool check_ippc_clients_connected(vx_producer producer)
{
    // if one of the receiver is ready, register it and the sender can start sending data
    vx_bool new_client_connected = vx_false_e;
    for (vx_uint32 i = 0U; i < VX_GC_NUM_CLIENTS; i++)
    {
        if (E_IPPC_OK == ippc_sender_receiver_ready(&producer->m_sender_ctx.m_sender, i) && 
            (producer->consumers_list[i].state == PROD_STATE_CLI_NOT_CONNECTED))
        {
            producer->consumers_list[i].state       = PROD_STATE_CLI_CONNECTED;
            producer->consumers_list[i].consumer_id = i;
            producer->prod_base.nb_consumers++;
            new_client_connected = vx_true_e;
        }
    
        if (producer->consumers_list[i].state == PROD_STATE_CLI_CONNECTED)
        {
            EIppcStatus l_status;

            VX_PRINT(
                VX_ZONE_INFO,
                "PRODUCER %s: send buffer metadata for consumer %u \n", producer->prod_base.name, i);

            // set up backchannel context
            producer->consumers_list[i].m_receiver_ctx.m_port_map = producer->ippc_port[i + 1U];
            producer->consumers_list[i].m_receiver_ctx.m_msg_size = sizeof(consumer_msg_content_t);
            producer->consumers_list[i].m_receiver_ctx.m_client_handler = producer_msg_handler;
            producer->consumers_list[i].m_receiver_ctx.m_application_ctx = producer;
            
            //create the backchannel connnector
            l_status  = ippc_registry_receiver_attach(&producer->m_shmem_ctx.m_registry,
                                                    &producer->consumers_list[i].m_receiver_ctx.m_receiver,
                                                    producer->consumers_list[i].m_receiver_ctx.m_port_map.m_port_id,
                                                    0,// always use receiver 0 for unicast ports;
                                                    producer->consumers_list[i].m_receiver_ctx.m_msg_size,
                                                    E_IPPC_RECEIVER_DISCARD_PAST);

            if(E_IPPC_OK == l_status)
            {
                l_status = ippc_registry_sync_attach(&producer->m_shmem_ctx.m_registry, &producer->consumers_list[i].m_receiver_ctx.m_sync, 
                    producer->consumers_list[i].m_receiver_ctx.m_port_map.m_receiver_index + 
                    VX_GC_NUM_CLIENTS);
            }

            if (E_IPPC_OK == l_status)
            {
                
                // launch backchannel thread, where we attach to the receiver of backchannel port
                int thread_status = pthread_create(&producer->consumers_list[i].bck_thread, NULL, producer_bck_thread, (void*)&producer->consumers_list[i]);
                if (thread_status == 0)
                {
                    producer->consumers_list[i].state = PROD_STATE_CLI_RUNNING;
                    VX_PRINT(
                        VX_ZONE_INFO,
                        "PRODUCER %s: consumer %u backchannel is ready, going to RUNNING state %u \n", producer->prod_base.name, i);
                }
                EIppcStatus l_status;
                pthread_mutex_lock(&producer->prod_base.client_mutex);
                producer_msg_content_t* buffid_message = ippc_shem_payload_pointer(&producer->m_sender_ctx, sizeof(producer_msg_content_t), &l_status);
                fill_reference_info(producer, buffid_message); 
                buffid_message->buffer_info.id                 = -1;
                buffid_message->buffer_info.metadata.is_valid  = 0;
                buffid_message->buffer_info.last_buffer        = producer->prod_base.last_buffer;
                buffid_message->buffer_info.metadata.size      = VX_GC_MAX_META_SIZE;
                send_id_message_consumers(producer, buffid_message, -1);
                pthread_mutex_unlock(&producer->prod_base.client_mutex);
            }
        }
    }
    return new_client_connected;
}

static void* producer_connection_check_thread(void* arg)
{
    vx_producer producer = (vx_producer)arg;
    char threadname[280U];
    snprintf(threadname, 280U, "producer_conn_check_thread_%s", producer->prod_base.name);
    pthread_setname_np(pthread_self(), threadname);

    VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: starting connection check thread \n", producer->prod_base.name);
    while(vx_false_e == producer->connection_check_polling_exit) // assume that after first dequeue, frequent polling for clients is no longer necessary
    {
        pthread_mutex_lock(&producer->prod_base.client_mutex);
        (void)check_ippc_clients_connected(producer);
        pthread_mutex_unlock(&producer->prod_base.client_mutex);
        tivxTaskWaitMsecs(producer->connection_check_polling_time);
    }
    VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: exiting connection check thread \n", producer->prod_base.name);

    return NULL;
}

static void* producer_broadcast_thread(void* arg)
{
    vx_producer producer = (vx_producer)arg;
    vx_reference dequeued_refs[VX_GC_MAX_NUM_REFS] = {0};
    vx_bool shutdown = (vx_bool)vx_false_e;
    vx_status status = (vx_status)VX_SUCCESS;

    char threadname[280U];
    snprintf(threadname, 280U, "%s_gc_broadcast_thread", producer->prod_base.name);
    pthread_setname_np(pthread_self(), threadname);

    while((vx_bool)vx_true_e != shutdown)
    {
        switch(producer->prod_base.state)
        {
            case VX_PROD_STATE_INIT:
            {
                // Enqueue all output buffer IDs so that graph can start processing
                for (vx_uint32 idx = 0; idx < producer->prod_base.num_buffers; idx++)
                {
                    producer->prod_base.enqueue_callback(producer->prod_base.graph_obj, producer->prod_base.refs[idx].ovx_ref);
                    producer->prod_base.nbEnqueueFrames++;
                    setBufferStatus(idx, IN_GRAPH, producer);
                }
                producer->prod_base.state = VX_PROD_STATE_RUN;
                VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: starting graph from inside producer!\n", producer->prod_base.name);
            }
            break;

            case VX_PROD_STATE_RUN:
            {
                vx_uint32 num_ready = 0;

                // go to cleanup if there is no consumer and last buffer flag is set
                if ((producer->prod_base.nb_consumers == 0) && (producer->prod_base.last_buffer == 1))
                {
                    producer->prod_base.state = VX_PROD_STATE_WAIT;
                    VX_PRINT(
                    VX_ZONE_INFO,
                    "PRODUCER %s: Consumer disconnected and last buffer signaled, shutting down! %s",
                    producer->prod_base.name,
                    "\n");
                    break;
                }

                // Dequeue from the Graph
                status = producer->prod_base.dequeue_callback(producer->prod_base.graph_obj, dequeued_refs, &num_ready);
                // update locked count for already locked refs
                updateLockedState(producer);
                producer->connection_check_polling_exit = vx_true_e; 
                if (status != (vx_status)VX_SUCCESS)
                {
                    break;
                }

                pthread_mutex_lock(&producer->prod_base.client_mutex);
                (void)check_ippc_clients_connected(producer);
                pthread_mutex_unlock(&producer->prod_base.client_mutex);
                for (vx_uint32 current_ref_num = 0; current_ref_num < num_ready; current_ref_num++)
                {
                    // Process one reference at a time
                    vx_reference ref_from_graph = dequeued_refs[current_ref_num];
                    producer->prod_base.nbDequeueFrames++;

                    if (producer->prod_base.nb_consumers == 0)
                    {
                        //  No client connected-  eneuque the buffer directly
                        VX_PRINT(
                            VX_ZONE_INFO,
                            "PRODUCER %s: consumer is not ready, enqueue the buffer again \n",
                            producer->prod_base.name);
                        producer->prod_base.enqueue_callback(producer->prod_base.graph_obj, ref_from_graph);
                        producer->prod_base.nbEnqueueFrames++;
                        if (producer->prod_base.last_buffer)
                        {
                            // not connected and last buffer - exit
                            VX_PRINT(
                                VX_ZONE_INFO,
                                "PRODUCER %s: last buffer received and we are async, exiting.....\n",
                                producer->prod_base.name);
                            producer->prod_base.state = VX_PROD_STATE_WAIT;
                            break;
                        }
                    }
                    else
                    {
                        VX_PRINT(
                            VX_ZONE_INFO,
                            "PRODUCER %s: dequeue output ref buffer %p\n",
                            producer->prod_base.name,
                            (vx_reference)ref_from_graph);

                        //  Get producer internal buffer id, and send it to the consumer
                        vx_int32 buffer_id = getBufferIdForProducer(ref_from_graph, producer);
                        if (buffer_id < 0)
                        {
                            VX_PRINT(
                                VX_ZONE_ERROR,
                                "PRODUCER %s: getBufferIdForProducer buffer not found; FATAL ERROR \n",
                                producer->prod_base.name);
                            shutdown = 1;
                            break;
                        }
                        else
                        {
                            EIppcStatus l_status;
                            pthread_mutex_lock(&producer->prod_base.client_mutex);
                            producer_msg_content_t* buffid_message = ippc_shem_payload_pointer(&producer->m_sender_ctx, sizeof(producer_msg_content_t), &l_status);
                            buffid_message->buffer_info.id                  = buffer_id;
                            buffid_message->buffer_info.metadata.is_valid   = 0;
                            buffid_message->buffer_info.last_buffer         = producer->prod_base.last_buffer;
                            buffid_message->buffer_info.metadata.size       = VX_GC_MAX_META_SIZE;
                            vx_size metadata_size                           = VX_GC_MAX_META_SIZE;

                            for (vx_uint32 i = 0U; i < VX_GC_NUM_CLIENTS; i++)
                            {
                                if (producer->consumers_list[i].state == PROD_STATE_CLI_NOT_CONNECTED)
                                {
                                    for (vx_uint32 j = 0; j < producer->prod_base.num_buffers; j++)
                                    {
                                        // the ref we want to unlock will always be locked here, since the consumer is disconnected
                                        // (at least one refcount is > 0 for locked)
                                        if ((producer->prod_base.refs[j].buffer_status == LOCKED) && (producer->prod_base.refs[j].attached_to_client[i] == 1))
                                        {
                                            VX_PRINT(
                                                VX_ZONE_WARNING,
                                                "PRODUCER %s: Reference %u, was in LOCKED state, trying to FREE \n",
                                                producer->prod_base.name,
                                                producer->prod_base.refs[j].ovx_ref);
                                            producer->prod_base.refs[j].attached_to_client[i] = 0;
                                            setBufferStatus(j, FREE, producer);
                                        }
                                    }
                                }
                            }
                            if (producer->prod_base.last_buffer)
                            {
                                // Last buffer (final frame) info was shared from the application
                                // The consumer needs this info to properly release the output buffer references.
                                VX_PRINT(
                                    VX_ZONE_INFO,
                                    "PRODUCER %s: send last frame signal to the consumer (%d) \n",
                                    producer->prod_base.name,
                                    producer->prod_base.nbDequeueFrames);
                                vx_int32 num_messages = send_id_message_consumers(producer, buffid_message, -1);
                                pthread_mutex_unlock(&producer->prod_base.client_mutex);
                                VX_PRINT(
                                    VX_ZONE_INFO,
                                    "PRODUCER %s: sent last buffer to %d consumers \n",
                                    producer->prod_base.name,
                                    num_messages);
                                // put the producer in waiting, then flushing mode
                                producer->prod_base.state = VX_PROD_STATE_WAIT;
                            }
                            else
                            {
                                // if at least one buffer (excluding the recently dequeued one) are
                                // occupied by graph, we can safely distribute the buffer to consumers                               
                                if (1U < getNumBufferWithStatus(producer, IN_GRAPH))
                                {
                                    // fetch metadata from producer reference and store
                                    if (NULL != producer->prod_base.transmit_meta)
                                    {
                                        status        = producer->prod_base.transmit_meta(
                                            producer->prod_base.graph_obj,
                                            ref_from_graph,
                                            (void*)buffid_message->metadata_buffer,
                                            &metadata_size);
                                        if (((vx_status)VX_SUCCESS != status) ||
                                            (metadata_size > VX_GC_MAX_META_SIZE)
                                        )
                                        {
                                            VX_PRINT(
                                                VX_ZONE_INFO,
                                                "PRODUCER %s: cannot get metadata OR metadata too large. \n",
                                                producer->prod_base.name);
                                            // metadata payload suppressed
                                            buffid_message->buffer_info.metadata.size = 0;
                                        }
                                        else
                                        {
                                            // curren value for metadata_size was set within getMetadataCallback
                                            buffid_message->buffer_info.metadata.is_valid = 1;
                                            buffid_message->buffer_info.metadata.size  = metadata_size;
                                        }
                                    }
                                    buffid_message->buffer_info.last_frame_dropped = producer->prod_base.last_frame_dropped;
                                    producer->prod_base.last_frame_dropped      = 0;

                                    // broadcast buffer to clients/consumers
                                    vx_int32 sent_messages =
                                        send_id_message_consumers(producer, buffid_message, buffer_id);
                                    pthread_mutex_unlock(&producer->prod_base.client_mutex);
                                    if (sent_messages == 0)
                                    {
                                        producer->prod_base.enqueue_callback(producer->prod_base.graph_obj, ref_from_graph);
                                        producer->prod_base.nbEnqueueFrames++;
                                        // ref_from_graph remains IN_GRAPH since it is enqueued back in the producer
                                        VX_PRINT(
                                            VX_ZONE_INFO,
                                            "PRODUCER %s: buffer ID was not sent to any consumer, %d enqueue back to graph\n", producer->prod_base.name,
                                            ref_from_graph);
                                    }
                                    else
                                    {
                                        // it was sent to at least one consumer
                                        VX_PRINT(
                                            VX_ZONE_INFO,
                                            "PRODUCER %s: objectbuffer ID %d sent to %d consumers\n",
                                            producer->prod_base.name,
                                            buffer_id,
                                            sent_messages);
                                    }
                                }
                                else
                                {
                                    // Frame dropped, since we want the producer to keep running
                                    VX_PRINT(
                                        VX_ZONE_WARNING,
                                        "PRODUCER %s: graph is running, no free buffer found, enqueue current buffer "
                                        "directly, amount of last frames dropped %d\n",
                                        producer->prod_base.name,
                                        producer->prod_base.last_frame_dropped);
                                    producer->prod_base.enqueue_callback(producer->prod_base.graph_obj, ref_from_graph);
                                    producer->prod_base.nbEnqueueFrames++;
                                    producer->prod_base.nbDroppedFrames++;
                                    producer->prod_base.last_frame_dropped++;
                                }
                            }
                        }
                    }
                }
            }
            break;

            case VX_PROD_STATE_WAIT:
            {
                vx_uint32 waitCount;
                // wait a fixed ammount of times to dequeue each reference, enabling the graph to shut down properly
                for (waitCount = 0; waitCount < producer->prod_base.num_buffers; waitCount++)
                {
                    vx_uint32 num_deque_refs = 0;

                    tivxTaskWaitMsecs(100);

                    // only dequeue if there is any reference in graph, to prevent forever blocking on dequeue
                    if (1U < getNumBufferWithStatus(producer, IN_GRAPH))
                    {
                        status = producer->prod_base.dequeue_callback(producer->prod_base.graph_obj, dequeued_refs, &num_deque_refs);
                        if (status != (vx_status)VX_SUCCESS)
                        {
                            // in case of error stop trying to dequeue
                            break;
                        }
                        setBufferStatus(getBufferIdForProducer(dequeued_refs[0U], producer), FREE, producer);
                        producer->prod_base.nbDequeueFrames += num_deque_refs;
                    }
                }

                // producer graph is flushed
                VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: graph flushed\n", producer->prod_base.name);
                producer->prod_base.state = VX_PROD_STATE_FLUSH;

                // linger a while before shutting the producer thread - there might be consumers
                // still working with the buffers sent by the producer. we dont want to release memory too early
                if (producer->prod_base.nb_consumers > 0)
                {
                    VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: waiting for consumers before shutdown \n", producer->prod_base.name);
                    tivxTaskWaitMsecs(200);
                }
            }
            break;

            case VX_PROD_STATE_FLUSH:
            {
                VX_PRINT(
                    VX_ZONE_INFO,
                    "PRODUCER %s: Output buffer stats:\nenqueued - %d \ndequeued - %d \ndropped - %d\n",
                    producer->prod_base.name,
                    producer->prod_base.nbEnqueueFrames,
                    producer->prod_base.nbDequeueFrames,
                    producer->prod_base.nbDroppedFrames);
                shutdown = (vx_bool)vx_true_e;
            }
            break;
        }
    }
    return NULL;
}

vx_status ownInitProducerObjectIppc(vx_producer producer, const vx_producer_params_t* params)
{
    vx_int32 l_status = 0;
    vx_status status = (vx_status)VX_SUCCESS;

    for (vx_uint32 i = 0U; i < VX_GC_NUM_CLIENTS; i++)
    {
        producer->consumers_list[i].state = PROD_STATE_CLI_NOT_CONNECTED;

    }
    producer->connection_check_polling_time = params->gc_params->connection_check_polling_time;
    producer->connection_check_polling_exit = vx_false_e; 

    for(vx_uint32 idx = 0U; idx < IPPC_PORT_COUNT; idx++)
    {
        producer->ippc_port[idx] = params->gc_params->ippc_port[idx];
    }

    l_status = ippc_shmem_init(producer->prod_base.access_point_name, producer->prod_base.num_buffers, producer->ippc_port,
                    IPPC_PORT_COUNT, sizeof(producer_msg_content_t), sizeof(consumer_msg_content_t), &producer->m_shmem_ctx);

    if(E_IPPC_OK == l_status)
    {
        SIppcPortMap l_portMap = producer->ippc_port[0];
        l_status = ippc_sender_init(&producer->m_shmem_ctx, &l_portMap, &producer->m_sender_ctx);
    }

    if(E_IPPC_OK != l_status)
    {
        status = (vx_status)VX_FAILURE;
    }

    return status;
}

vx_status releaseProducerIppc(vx_producer* producer)
{
    int status;
    vx_producer this_producer = producer[0];
    status = pthread_join(this_producer->prod_base.broadcast_thread, NULL);
    ippc_shmem_deinit(&this_producer->m_shmem_ctx, this_producer->prod_base.access_point_name);
    return ((vx_status)status);
}

vx_status producerStartIppc(vx_producer producer)
{
    /* start the ippc broadcasting thread */
    int thread_status = pthread_create(&producer->prod_base.broadcast_thread, NULL, producer_broadcast_thread, (void*)producer);
    if (0U != thread_status)
    {
        VX_PRINT(VX_ZONE_ERROR, "error creating producer_broadcast_thread! \n");
    }
    else
    {
        thread_status = pthread_create(&producer->connection_check_thread, NULL, producer_connection_check_thread, (void*)producer);
    }
    return ((vx_status)thread_status);
}

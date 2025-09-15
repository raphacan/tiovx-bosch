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
static vx_int32 send_reference_info(vx_producer producer, client_context* client);
static vx_int32 add_client(vx_producer producer, client_context* connection, uint64_t consumer_id);
static void drop_client(vx_producer producer, producer_bckchannel_t* client, vx_int32 client_num);
static void handle_clients(void* clientPtr, void* data);
static void* producer_broadcast_thread(void* arg);

static vx_int32 send_id_message_consumers(
                                            vx_producer producer,
                                            producer_msg_content_t* msg,
                                            vx_int32 buff_id)
{
    vx_int32 status = 0;
    vx_int32 sent_to_consumer = 0;
    vx_uint32 locked_cnt = 0U;
    vx_uint32 mask = 0U;
    vx_uint8 message_buffer[SOCKET_MAX_MSG_SIZE];

    // append buffer ID message and all metadata to the buffer
    memcpy(&message_buffer[0], msg, sizeof(producer_msg_content_t));
    memcpy(&message_buffer[0] + sizeof(producer_msg_content_t), &producer->metadata_buffer, msg->buffer_info.metadata.size);

    pthread_mutex_lock(&producer->prod_base.client_mutex);

    for (vx_uint32 i = 0; i < VX_GC_NUM_CLIENTS; i++)
    {          
        if (
            (producer->consumers_list[i].socket_fd > 0) &&
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

                status = socket_write(producer->consumers_list[i].socket_fd, message_buffer, NULL, 0);
                if (status == SOCKET_STATUS_OK)
                {
                    // copy reference sent to the consumers
                    sent_to_consumer++;
                    if (buff_id != -1)
                    {
                        VX_PRINT(
                            VX_ZONE_INFO,
                            "PRODUCER %s: buffer ID sent to consumer %d with consumer_id %d\n",
                            producer->prod_base.name,
                            i,
                            producer->consumers_list[i].consumer_id);
                    }
                }
                else
                {
                    VX_PRINT(
                        VX_ZONE_ERROR, "PRODUCER %s: buffer ID could not be sent to consumer %d \n", producer->prod_base.name, i);
                    producer->consumers_list[i].state = PROD_STATE_CLI_FAILED;
                    status = VX_FAILURE;
                    // the failure here should result in a socket timeout/early close for the other thread
                    // this is why no special error handling is needed in the graph thread
                }
            }
        }
    }

    pthread_mutex_unlock(&producer->prod_base.client_mutex);

    return sent_to_consumer;
}

static vx_int32 send_reference_info(vx_producer producer, client_context* client)
{
    vx_int32 status = SOCKET_STATUS_FAILURE;

    if (sizeof(producer_msg_content_t) >= SOCKET_MAX_MSG_SIZE)
    {
        VX_PRINT(
            VX_ZONE_ERROR, "PRODUCER: Cannot transmit TIVX object data, insufficient socket message size %s", "\n");
        return status;
    }

    for (vx_uint32 i = 0; i < producer->prod_base.num_buffer_refs_export; i++)
    {
        vx_enum                  ref_type;
        vx_uint32                num_items          = 0;
        tivx_utils_ref_ipc_msg_t ipc_message_parent = {0};
        tivx_utils_ref_ipc_msg_t ipc_message_item[VX_GC_MAX_NUM_REFS];

        vx_uint8               message_buffer[SOCKET_MAX_MSG_SIZE];
        producer_msg_content_t* buffer_desc_msg = (producer_msg_content_t*)&message_buffer;
        buffer_desc_msg->msg_type             = VX_MSGTYPE_REF_BUF;
        buffer_desc_msg->buffer_info.last_buffer  = 0;
        buffer_desc_msg->buffer_info.num_items    = 0;

        vx_status framework_status =
            vxQueryReference(producer->prod_base.refs[i].ovx_ref, VX_REFERENCE_TYPE, (void*)&ref_type, (vx_size)sizeof(ref_type));
        if (framework_status != VX_SUCCESS)
        {
            VX_PRINT(VX_ZONE_ERROR, "PRODUCER: vxQueryReference() failed for object [%d]\n", i);
            break;
        }
        else if (ref_type == VX_TYPE_OBJECT_ARRAY)
        {
            framework_status = vx_utils_export_ref_for_ipc_xfer_objarray(
                producer->prod_base.refs[i].ovx_ref,
                &num_items,
                &ipc_message_parent,
                (tivx_utils_ref_ipc_msg_t*)&ipc_message_item[0]);

            buffer_desc_msg->buffer_info.num_items = num_items;

            // send object array items data, if present
            for (vx_uint32 j = 0; j < num_items; j++)
            {
                buffer_desc_msg->item_index = j;
                memcpy(&buffer_desc_msg->ref_export_handle, (void*)&ipc_message_item[j], sizeof(tivx_utils_ref_ipc_msg_t));

                VX_PRINT(
                    VX_ZONE_INFO,
                    "PRODUCER %s: [VX_MSGTYPE_REF_BUF] sending objarray element %d with fd count %d\n",
                    producer->prod_base.name,
                    j,
                    buffer_desc_msg->ref_export_handle.numFd);

                status = socket_write(
                    client->socket_fd,
                    (vx_uint8*)buffer_desc_msg,
                    (vx_int32*)buffer_desc_msg->ref_export_handle.fd,
                    buffer_desc_msg->ref_export_handle.numFd);
                if (status != SOCKET_STATUS_OK)
                {
                    VX_PRINT(
                        VX_ZONE_ERROR,
                        "PRODUCER %s: send_reference_info() failed while sending socket message\n",
                        producer->prod_base.name);
                    return status;
                }
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

        VX_PRINT(
            VX_ZONE_INFO,
            "PRODUCER %s: Sending [VX_MSGTYPE_REF_BUF] for buffer %d of type %d\n",
            producer->prod_base.name,
            i,
            ref_type);

        if (i == (producer->prod_base.num_buffer_refs_export - 1))
        {
            VX_PRINT(
                VX_ZONE_INFO, "PRODUCER %s: number of objects to exchange reached, set last object\n", producer->prod_base.name);
            buffer_desc_msg->buffer_info.last_buffer = 1;
        }

        // send reference data, for object array this is final metadata
        buffer_desc_msg->item_index = 0; // used only for object array items
        memcpy(&buffer_desc_msg->ref_export_handle, &ipc_message_parent, sizeof(tivx_utils_ref_ipc_msg_t));

        status = socket_write(
            client->socket_fd,
            (vx_uint8*)buffer_desc_msg,
            (vx_int32*)buffer_desc_msg->ref_export_handle.fd,
            buffer_desc_msg->ref_export_handle.numFd);

        if (status != SOCKET_STATUS_OK)
        {
            VX_PRINT(
                VX_ZONE_ERROR,
                "PRODUCER %s: send_reference_info() failed while sending socket message\n",
                producer->prod_base.name);
            break;
        }
    }

    return status;
}

static vx_int32 add_client(vx_producer producer, client_context* connection, uint64_t consumer_id)
{
    vx_int32      client_num = -1;
    producer_bckchannel_t* client     = NULL;

    pthread_mutex_lock(&producer->prod_base.client_mutex);

    for (vx_uint32 i = 0; i < VX_GC_NUM_CLIENTS; i++)
    {
        if (producer->consumers_list[i].state == PROD_STATE_CLI_NOT_CONNECTED)
        {
            client = &producer->consumers_list[i];

            client->state       = PROD_STATE_CLI_CONNECTED;
            client->consumer_id = consumer_id;
            client->socket_fd   = connection->socket_fd;
            client_num          = i;

            producer->prod_base.nb_consumers++;
            break;
        }
    }

    if ((NULL == client) || (client_num == -1))
    {
        VX_PRINT(
            VX_ZONE_ERROR, "PRODUCER %s: Maximum number of clients reached or error in client state\n", producer->prod_base.name);
    }

    pthread_mutex_unlock(&producer->prod_base.client_mutex);
    return client_num;
}

static void drop_client(vx_producer producer, producer_bckchannel_t* client, vx_int32 client_num)
{
    pthread_mutex_lock(&producer->prod_base.client_mutex);

    VX_PRINT(VX_ZONE_INFO, "PRODUCER: Cleaning up client with socket %d and PID %d\n", client->socket_fd, client->consumer_id);

    if (client->state != PROD_STATE_CLI_NOT_CONNECTED)
    {
        // zero out client info
        client->state                 = PROD_STATE_CLI_NOT_CONNECTED;
        client->first_buffer_released = 0;
        client->consumer_id                   = 0;
        client->socket_fd             = 0;
        producer->prod_base.nb_consumers--;

        for (vx_uint32 i = 0; i < producer->prod_base.num_buffers; i++)
        {
            // the ref we want to unlock will always be locked here, since the consumer is disconnected
            // (at least one refcount is > 0 for locked)
            if ((producer->prod_base.refs[i].buffer_status == LOCKED) && (producer->prod_base.refs[i].attached_to_client[client_num] == 1))
            {
                VX_PRINT(
                    VX_ZONE_WARNING,
                    "PRODUCER %s: Reference %u, was in LOCKED state, trying to FREE \n",
                    producer->prod_base.name,
                    i,
                    producer->prod_base.refs[i].buffer_status);
                producer->prod_base.refs[i].attached_to_client[client_num] = 0;
                vx_int32 status = setBufferStatus(i, FREE, producer);
                if (status != VX_SUCCESS)
                {
                    VX_PRINT(
                        VX_ZONE_ERROR, "PRODUCER %s: Reference %u, could not be set to FREE \n", producer->prod_base.name, i);
                }
            }
        }
    }

    pthread_mutex_unlock(&producer->prod_base.client_mutex);

}

static void handle_clients(void* clientPtr, void* data)
{
    // this function must be MT-safe
    client_context* client   = (client_context*)clientPtr;
    vx_producer producer = (vx_producer)data;

    vx_uint8           message_buffer[SOCKET_MAX_MSG_SIZE];
    producer_msg_content_t* consumer_message;

    vx_int32 client_num = -1;
    vx_int32 status     = SOCKET_STATUS_OK;

    while (1)
    {
        if (producer->prod_base.last_buffer == 1U)
        {
            VX_PRINT(
                VX_ZONE_INFO, "PRODUCER %s: reconfiguring the socket timeouts for release %s", producer->prod_base.name, "\n");
            socket_reconfigure_timeout(client->socket_fd, SOCKET_TIMEOUT_USECS_RELEASE);
        }
        // block until data is ready
        status = socket_read(client->socket_fd, message_buffer, NULL, NULL);
        if ((status < SOCKET_STATUS_OK) || (status == SOCKET_STATUS_PEER_CLOSED))
        {
            VX_PRINT(VX_ZONE_ERROR, "PRODUCER %s: socket_read() timed out or error\n", producer->prod_base.name);
            status = SOCKET_STATUS_FAILURE;
            break;
        }

        // handle message
        consumer_message = (producer_msg_content_t*)message_buffer;

        switch (consumer_message->msg_type)
        {
        case VX_MSGTYPE_HELLO:
            client_num = add_client(producer, client, consumer_message->consumer_id);
            if (client_num >= 0)
            {
                VX_PRINT(
                    VX_ZONE_INFO, "PRODUCER %s:Received [VX_MSGTYPE_HELLO] from client %d\n", producer->prod_base.name, client_num);
                VX_PRINT(
                    VX_ZONE_INFO,
                    " [UPT] First Time Connected to Producer %s with ID %u \n ",
                    producer->prod_base.name,
                    consumer_message->consumer_id);

                status = send_reference_info(producer, client);
                if (SOCKET_STATUS_OK == status)
                {
                    VX_PRINT(VX_ZONE_INFO, "PRODUCER: all buffers sent to client %d\n", client_num);
                    producer->consumers_list[client_num].state = PROD_STATE_CLI_RUNNING;
                }
                else
                {
                }
            }
            else
            {
                status = SOCKET_STATUS_FAILURE;
            }
            break;

        case VX_MSGTYPE_BUF_RELEASE:
        {
            if (producer->consumers_list[client_num].first_buffer_released == 0)
            {
                // due to long delays in setting up the producer/consumer communication, there are different timeout
                // values for init/streaming phases
                VX_PRINT(
                    VX_ZONE_INFO,
                    "PRODUCER %s: reconfiguring the socket timeouts for streaming values %s",
                    producer->prod_base.name,
                    "\n");

                socket_reconfigure_timeout(client->socket_fd, SOCKET_TIMEOUT_USECS_STREAMING);
                producer->consumers_list[client_num].first_buffer_released = 1;
            }

            producer_msg_content_t* bufferid_message = (producer_msg_content_t*)message_buffer;
            if (bufferid_message->buffer_info.last_buffer == 1)
            {
                // this client graph is flushed, shut the current client thread down
                producer->consumers_list[client_num].state = PROD_STATE_CLI_FLUSHED;
                producer->prod_base.refs[bufferid_message->buffer_info.id].attached_to_client[client_num] = 0;
                status = SOCKET_STATUS_FAILURE;
            }
            else
            {
                VX_PRINT(
                    VX_ZONE_INFO,
                    "PRODUCER %s:Received [VX_MSGTYPE_BUF_RELEASE] release id: %d from client %d \n",
                    producer->prod_base.name,
                    bufferid_message->buffer_info.id,
                    client_num);
                vx_reference next_out_ref = producer->prod_base.refs[bufferid_message->buffer_info.id].ovx_ref;
                if (next_out_ref != NULL)
                {
                    // enqueue the new buffer in the handle producer thread, here the refcount is decreased
                    producer->prod_base.refs[bufferid_message->buffer_info.id].attached_to_client[client_num] = 0;
                    if (VX_SUCCESS != setBufferStatus(bufferid_message->buffer_info.id, FREE, producer))
                    {
                        VX_PRINT(
                            VX_ZONE_ERROR,
                            "PRODUCER %s: cannot release buffer ID %d\n",
                            producer->prod_base.name,
                            bufferid_message->buffer_info.id);
                    }
                }
                else
                {
                    VX_PRINT(VX_ZONE_ERROR, "PRODUCER %s: buffer ID not valid.\n", producer->prod_base.name);
                    status = SOCKET_STATUS_FAILURE;
                }
            }
        }
        break;

        case VX_MSGTYPE_CONSUMER_CREATE_DONE: // consumer notifys about graph creation being completed
        {
            VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: received VX_MSGTYPE_CONSUMER_CREATE_DONE state from consumer %d \n", producer->prod_base.name, consumer_message->consumer_id);
            producer->consumers_list[consumer_message->consumer_id].state       = PROD_STATE_CLI_GRAPH_VERIFIED; 
            status = SOCKET_STATUS_OK;
        }
        break;

        default:
            VX_PRINT(
                VX_ZONE_ERROR,
                "PRODUCER %s: Received [UNKNOWN MESSAGE] %d\n",
                producer->prod_base.name,
                consumer_message->msg_type);
            status = SOCKET_STATUS_FAILURE;
            break;
        }

        if (status != SOCKET_STATUS_OK)
        {
            break;
        }
    }

    // clean up client
    if (client_num >= 0)
    {
        drop_client(producer, &producer->consumers_list[client_num], client_num);
    }

    VX_PRINT(VX_ZONE_INFO, "PRODUCER: client %d thread shutting down %s", client_num, "\n");
    return;
}

static void* producer_broadcast_thread(void* arg)
{
    vx_producer producer = (vx_producer)arg;
    vx_reference dequeued_refs[VX_GC_MAX_NUM_REFS] = {0};
    vx_bool shutdown = (vx_bool)vx_false_e;
    vx_status status = (vx_status)VX_SUCCESS;

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
                if (status != (vx_status)VX_SUCCESS)
                {
                    break;
                }
              
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
                            producer_msg_content_t buffid_message = {0};
                            buffid_message.msg_type                       = VX_MSGTYPE_BUFID_CMD;
                            buffid_message.buffer_info.id                 = buffer_id;
                            buffid_message.buffer_info.metadata.is_valid  = 0;
                            buffid_message.buffer_info.last_buffer        = producer->prod_base.last_buffer;
                            buffid_message.buffer_info.metadata.size      = 0;

                            vx_size metadata_size              = SOCKET_MAX_MSG_SIZE - sizeof(buffid_message);
                            if (producer->prod_base.last_buffer)
                            {
                                // Last buffer (final frame) info was shared from the application
                                // The consumer needs this info to properly release the output buffer references.
                                VX_PRINT(
                                    VX_ZONE_INFO,
                                    "PRODUCER %s: send last frame signal to the consumer (%d) \n",
                                    producer->prod_base.name,
                                    producer->prod_base.nbDequeueFrames);
                                vx_int32 num_messages = send_id_message_consumers(producer, &buffid_message, -1);
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
                                            (void*)producer->metadata_buffer,
                                            &metadata_size);
                                        if (((vx_status)VX_SUCCESS != status) ||
                                            (metadata_size + sizeof(producer_msg_content_t) > SOCKET_MAX_MSG_SIZE)
                                        )
                                        {
                                            VX_PRINT(
                                                VX_ZONE_INFO,
                                                "PRODUCER %s: cannot get metadata OR metadata too large. \n",
                                                producer->prod_base.name);
                                            // metadata payload suppressed
                                            buffid_message.buffer_info.metadata.size = 0;
                                        }
                                        else
                                        {
                                            // curren value for metadata_size was set within transmit_meta
                                            buffid_message.buffer_info.metadata.is_valid = 1;
                                            buffid_message.buffer_info.metadata.size  = metadata_size;
                                        }
                                    }
                                    buffid_message.buffer_info.last_frame_dropped = producer->prod_base.last_frame_dropped;
                                    producer->prod_base.last_frame_dropped        = 0;

                                    // broadcast buffer to clients/consumers
                                    vx_int32 sent_messages =
                                        send_id_message_consumers(producer, &buffid_message, buffer_id);
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

vx_status ownInitProducerObjectSock(vx_producer producer, const vx_producer_params_t* params)
{
    vx_int32 l_status = 0;
    vx_status status = (vx_status)VX_SUCCESS;


    for (vx_uint32 i = 0U; i < VX_GC_NUM_CLIENTS; i++)
    {
        producer->consumers_list[i].state = PROD_STATE_CLI_NOT_CONNECTED;
        producer->consumers_list[i].socket_fd = 0;
    }

    producer->server.socket_name    = producer->prod_base.access_point_name;
    producer->server.client_arg     = (void*)producer;
    producer->server.client_handler = handle_clients;
    l_status                        = socket_server_create(&producer->server);

    if (l_status < 0)
    {
        VX_PRINT(VX_ZONE_ERROR, "PRODUCER: socket_server_create() failed for master channel.\n");
        status = (vx_status)VX_FAILURE;
    }
    
    return status;
}

vx_status releaseProducerSock(vx_producer* producer)
{
    int status;
    vx_producer this_producer = producer[0];
    status = pthread_join(this_producer->prod_base.broadcast_thread, NULL);
    socket_server_close(&this_producer->server);
    return (vx_status)status;
}

vx_status producerStartSock(vx_producer producer)
{
    /* start the ippc broadcasting thread */
    int thread_status = pthread_create(&producer->prod_base.broadcast_thread, NULL, producer_broadcast_thread, (void*)producer);
    if (0U != thread_status)
    {
        VX_PRINT(VX_ZONE_ERROR, "error creating producer_broadcast_thread! \n");
    }
    return ((vx_status)thread_status);
}

#include <vx_internal.h>

static int32_t send_reference_info(vx_producer producer, client_context* client)
{
    int32_t status = VX_GW_STATUS_FAILURE;

    if (sizeof(vx_gw_buff_desc_msg) >= SOCKET_MAX_MSG_SIZE)
    {
        VX_PRINT(
            VX_ZONE_ERROR, "PRODUCER: Cannot transmit TIVX object data, insufficient socket message size %s", "\n");
        return status;
    }

    for (uint32_t i = 0; i < producer->numBufferRefsExport; i++)
    {
        vx_enum                  ref_type;
        vx_uint32                num_items          = 0;
        tivx_utils_ref_ipc_msg_t ipc_message_parent = {0};
        tivx_utils_ref_ipc_msg_t ipc_message_item[VX_GW_MAX_NUM_REFS];

        uint8_t               message_buffer[SOCKET_MAX_MSG_SIZE];
        vx_gw_buff_desc_msg* buffer_desc_msg = (vx_gw_buff_desc_msg*)&message_buffer;
        buffer_desc_msg->msg_type             = VX_MSGTYPE_REF_BUF;
        buffer_desc_msg->last_reference       = 0;
        buffer_desc_msg->num_items            = 0;

        vx_status framework_status =
            vxQueryReference(producer->refs[i].ovx_ref, VX_REFERENCE_TYPE, (void*)&ref_type, (vx_size)sizeof(ref_type));
        if (framework_status != VX_SUCCESS)
        {
            VX_PRINT(VX_ZONE_ERROR, "PRODUCER: vxQueryReference() failed for object [%d]\n", i);
            break;
        }
        else if (ref_type == VX_TYPE_OBJECT_ARRAY)
        {
            framework_status = rbvx_utils_export_ref_for_ipc_xfer_objarray(
                producer->refs[i].ovx_ref,
                &num_items,
                &ipc_message_parent,
                (tivx_utils_ref_ipc_msg_t*)&ipc_message_item[0]);

            buffer_desc_msg->num_items = num_items;

            // send object array items data, if present
            for (uint32_t j = 0; j < num_items; j++)
            {
                buffer_desc_msg->item_index = j;
                memcpy(&buffer_desc_msg->ref_export_handle, (void*)&ipc_message_item[j], sizeof(tivx_utils_ref_ipc_msg_t));

                VX_PRINT(
                    VX_ZONE_INFO,
                    "PRODUCER %s: [VX_MSGTYPE_REF_BUF] sending objarray element %d with fd count %d\n",
                    producer->name,
                    j,
                    buffer_desc_msg->ref_export_handle.numFd);

                status = socket_write(
                    client->socket_fd,
                    (uint8_t*)buffer_desc_msg,
                    (int32_t*)buffer_desc_msg->ref_export_handle.fd,
                    buffer_desc_msg->ref_export_handle.numFd);
                if (status != SOCKET_STATUS_OK)
                {
                    VX_PRINT(
                        VX_ZONE_ERROR,
                        "PRODUCER %s: send_reference_info() failed while sending socket message\n",
                        producer->name);
                    return status;
                }
            }
        }
        else
        {
            framework_status = tivx_utils_export_ref_for_ipc_xfer(producer->refs[i].ovx_ref, &ipc_message_parent);
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
                producer->name,
                i + 1,
                producer->numBufferRefsExport);
        }

        VX_PRINT(
            VX_ZONE_INFO,
            "PRODUCER %s: Sending [VX_MSGTYPE_REF_BUF] for buffer %d of type %d\n",
            producer->name,
            i,
            ref_type);

        if (i == (producer->numBufferRefsExport - 1))
        {
            VX_PRINT(
                VX_ZONE_INFO, "PRODUCER %s: number of objects to exchange reached, set last object\n", producer->name);
            buffer_desc_msg->last_reference = 1;
        }

        // send reference data, for object array this is final metadata
        buffer_desc_msg->item_index = 0; // used only for object array items
        memcpy(&buffer_desc_msg->ref_export_handle, &ipc_message_parent, sizeof(tivx_utils_ref_ipc_msg_t));

        status = socket_write(
            client->socket_fd,
            (uint8_t*)buffer_desc_msg,
            (int32_t*)buffer_desc_msg->ref_export_handle.fd,
            buffer_desc_msg->ref_export_handle.numFd);

        if (status != SOCKET_STATUS_OK)
        {
            VX_PRINT(
                VX_ZONE_ERROR,
                "PRODUCER %s: send_reference_info() failed while sending socket message\n",
                producer->name);
            break;
        }
    }

    return status;
}

static int32_t add_client(vx_producer producer, client_context* connection, uint64_t consumer_id)
{
    int32_t      client_num = -1;
    producer_bckchannel_t* client     = NULL;

    pthread_mutex_lock(&producer->client_mutex);

    for (uint32_t i = 0; i < VX_GW_NUM_CLIENTS; i++)
    {
        if (producer->consumers_list[i].state == PROD_STATE_CLI_NOT_CONNECTED)
        {
            client = &producer->consumers_list[i];

            client->state       = PROD_STATE_CLI_CONNECTED;
            client->consumer_id = consumer_id;
            client->socket_fd   = connection->socket_fd;
            client_num          = i;

            producer->nb_consumers++;
            break;
        }
    }

    if ((NULL == client) || (client_num == -1))
    {
        VX_PRINT(
            VX_ZONE_ERROR, "PRODUCER %s: Maximum number of clients reached or error in client state\n", producer->name);
    }

    pthread_mutex_unlock(&producer->client_mutex);
    return client_num;
}

static void drop_client(vx_producer producer, producer_bckchannel_t* client, int32_t client_num)
{
    pthread_mutex_lock(&producer->client_mutex);

    VX_PRINT(VX_ZONE_INFO, "PRODUCER: Cleaning up client with socket %d and PID %d\n", client->socket_fd, client->consumer_id);

    if (client->state != PROD_STATE_CLI_NOT_CONNECTED)
    {
        // zero out client info
        client->state                 = PROD_STATE_CLI_NOT_CONNECTED;
        client->first_buffer_released = 0;
        client->consumer_id                   = 0;
        client->socket_fd             = 0;
        producer->nb_consumers--;

        for (uint32_t i = 0; i < producer->numBuffers; i++)
        {
            // the ref we want to unlock will always be locked here, since the consumer is disconnected
            // (at least one refcount is > 0 for locked)
            if ((producer->refs[i].buffer_status == LOCKED) && (producer->refs[i].attached_to_client[client_num] == 1))
            {
                VX_PRINT(
                    VX_ZONE_WARNING,
                    "PRODUCER %s: Reference %u, was in LOCKED state, trying to FREE \n",
                    producer->name,
                    i,
                    producer->refs[i].buffer_status);
                producer->refs[i].attached_to_client[client_num] = 0;
                int32_t status = set_buffer_status(producer->refs[i].ovx_ref, FREE, producer);
                if (status != VX_GW_STATUS_SUCCESS)
                {
                    VX_PRINT(
                        VX_ZONE_ERROR, "PRODUCER %s: Reference %u, could not be set to FREE \n", producer->name, i);
                }
            }
        }
    }

    pthread_mutex_unlock(&producer->client_mutex);

}

void handle_clients(void* clientPtr, void* data)
{
    // this function must be MT-safe
    client_context* client   = (client_context*)clientPtr;
    vx_producer producer = (vx_producer)data;

    uint8_t           message_buffer[SOCKET_MAX_MSG_SIZE];
    vx_gw_hello_msg* consumer_message;

    int32_t client_num = -1;
    int32_t status     = VX_GW_STATUS_SUCCESS;

    while (1)
    {
        if (producer->last_buffer == 1U)
        {
            VX_PRINT(
                VX_ZONE_INFO, "PRODUCER %s: reconfiguring the socket timeouts for release %s", producer->name, "\n");
            socket_reconfigure_timeout(client->socket_fd, SOCKET_TIMEOUT_USECS_RELEASE);
        }
        // block until data is ready
        status = socket_read(client->socket_fd, message_buffer, NULL, NULL);
        if ((status < SOCKET_STATUS_OK) || (status == SOCKET_STATUS_PEER_CLOSED))
        {
            VX_PRINT(VX_ZONE_ERROR, "PRODUCER %s: socket_read() timed out or error\n", producer->name);
            status = VX_GW_STATUS_FAILURE;
            break;
        }

        // handle message
        consumer_message = (vx_gw_hello_msg*)message_buffer;

        switch (consumer_message->msg_type)
        {
        case VX_MSGTYPE_HELLO:
            client_num = add_client(producer, client, consumer_message->consumer_id);
            if (client_num >= 0)
            {
                VX_PRINT(
                    VX_ZONE_INFO, "PRODUCER %s:Received [VX_MSGTYPE_HELLO] from client %d\n", producer->name, client_num);
                VX_PRINT(
                    VX_ZONE_PERF,
                    " [UPT] First Time Connected to Producer %s with ID %u \n ",
                    producer->name,
                    consumer_message->consumer_id);

                status = send_reference_info(producer, client);
                if (VX_GW_STATUS_SUCCESS == status)
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
                status = VX_GW_STATUS_FAILURE;
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
                    producer->name,
                    "\n");

                socket_reconfigure_timeout(client->socket_fd, SOCKET_TIMEOUT_USECS_STREAMING);
                producer->consumers_list[client_num].first_buffer_released = 1;
            }

            vx_gw_buff_id_msg* bufferid_message = (vx_gw_buff_id_msg*)message_buffer;
            if (bufferid_message->last_buffer == 1)
            {
                // this client graph is flushed, shut the current client thread down
                producer->consumers_list[client_num].state = PROD_STATE_CLI_FLUSHED;
                producer->refs[bufferid_message->buffer_id].attached_to_client[client_num] = 0;
                status = VX_GW_STATUS_CONSUMER_FLUSHED;
            }
            else
            {
                VX_PRINT(
                    VX_ZONE_INFO,
                    "PRODUCER %s:Received [VX_MSGTYPE_BUF_RELEASE] release id: %d from client %d \n",
                    producer->name,
                    bufferid_message->buffer_id,
                    client_num);
                vx_reference next_out_ref = producer->refs[bufferid_message->buffer_id].ovx_ref;
                if (next_out_ref != NULL)
                {
                    // enqueue the new buffer in the handle producer thread, here the refcount is decreased
                    producer->refs[bufferid_message->buffer_id].attached_to_client[client_num] = 0;
                    status = set_buffer_status(next_out_ref, FREE, producer);
                    if (status != VX_GW_STATUS_SUCCESS)
                    {
                        VX_PRINT(
                            VX_ZONE_ERROR,
                            "PRODUCER %s: cannot release buffer ID %d\n",
                            producer->name,
                            bufferid_message->buffer_id);
                    }
                }
                else
                {
                    VX_PRINT(VX_ZONE_ERROR, "PRODUCER %s: buffer ID not valid.\n", producer->name);
                    status = VX_GW_STATUS_FAILURE;
                }
            }
        }
        break;

        case VX_MSGTYPE_CONSUMER_CREATE_DONE: // consumer notifys about graph creation being completed
        {
            VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: received VX_MSGTYPE_CONSUMER_CREATE_DONE state from consumer %d \n", producer->name, consumer_message->consumer_id);
            producer->consumers_list[consumer_message->consumer_id].state       = PROD_STATE_CLI_GRAPH_VERIFIED; 
            status = VX_GW_STATUS_SUCCESS;
        }
        break;

        default:
            VX_PRINT(
                VX_ZONE_ERROR,
                "PRODUCER %s: Received [UNKNOWN MESSAGE] %d\n",
                producer->name,
                consumer_message->msg_type);
            status = VX_GW_STATUS_FAILURE;
            break;
        }

        if (status != VX_GW_STATUS_SUCCESS)
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

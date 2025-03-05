#include <VX/vx_producer_ippc.h>

static vx_int32 send_id_message_consumers(
                                            vx_producer producer,
                                            vx_prod_msg_content_t* msg,
                                            buffer_info_t* ref)
    // in case mask is 0 (all consumers lock maximum amount of buffers allowed), 
    // free reference from here so producer can find it as an available ref
    if ((0U == mask) && (ref != NULL))
    {
        set_buffer_status(ref->ovx_ref, FREE, producer);
    }
    if (msg != NULL)
    {
        msg->mask = mask;
        VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: set mask in payload %d\n", producer->name, mask);
    }   

    status = ippc_shem_send(&producer->m_sender_ctx);
    if (status == E_IPPC_OK)
    {
        sent_to_consumer = VX_GW_NUM_CLIENTS;
        VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: buffer ID sent to consumers %d\n", producer->name);
    }
    else
    {
        VX_PRINT(VX_ZONE_ERROR, "PRODUCER %s: buffer ID could not be sent to consumers \n", producer->name);
    }
}

void* producer_bck_thread(void* arg)
{
    producer_bckchannel_t* l_consumer = (producer_bckchannel_t*) arg;

    char threadname[280U];
    snprintf(threadname, 280U, "producer_bck_thread_%u", l_consumer->consumer_id);
    pthread_setname_np(pthread_self(), threadname);

    VX_PRINT(VX_ZONE_INFO, "PRODUCER : starting backchannel worker for consumer %u on port %u\n", l_consumer->consumer_id, 
                                                                       l_consumer->m_receiver_ctx.m_port_map.m_port_id);
    while(l_consumer->state != PROD_STATE_CLI_FLUSHED)
    {
        // wait for message on backchannel
        ippc_receive(&l_consumer->m_receiver_ctx);
    }

    return NULL;
}

static void fill_reference_info(vx_producer producer, vx_prod_msg_content_t* buffid_message)
{
    buffid_message->num_refs = producer->numBufferRefsExport;
    uint32_t i = 0;
    uint32_t j = 0;
    for (i = 0; i < producer->numBufferRefsExport; i++)
    {
        vx_enum                  ref_type;
        vx_uint32                num_items          = 0;
        tivx_utils_ref_ipc_msg_t ipc_message_parent = {0};
        tivx_utils_ref_ipc_msg_t ipc_message_item[VX_GW_MAX_NUM_REFS];

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

            buffid_message->num_items = num_items;

            // send object array items data, if present
            for (j = 0; j < num_items; j++)
            {
                buffid_message->ref_export_handle[i][j] = ipc_message_item[j];

                VX_PRINT(
                    VX_ZONE_INFO,
                    "PRODUCER %s: sending objarray element %d with fd count %d\n",
                    producer->name,
                    j,
                    buffid_message->ref_export_handle[i][j].numFd);
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

        // send reference data, for object array this is final metadata
        buffid_message->ref_export_handle[i][j] = ipc_message_parent;
    }

}

void producer_msg_handler(const void * producer_p, const void * data_p, uint8_t last_buffer_from_series)
{
    const vx_cons_msg_content_t* const received_msg = (const vx_cons_msg_content_t*)data_p;
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
                // this client graph is flushed, shut the current client thread down
                producer->refs[received_msg->buffer_id].attached_to_client[received_msg->consumer_id] = 0;
            }
            else
            {
                VX_PRINT(
                    VX_ZONE_INFO,
                    "PRODUCER %s:Received release id: %d from consumer %d \n",
                    producer->name,
                    received_msg->buffer_id,
                    received_msg->consumer_id);
                vx_reference next_out_ref = producer->refs[received_msg->buffer_id].ovx_ref;
                if (next_out_ref != NULL)
                {
                    // enqueue the new buffer in the handle producer thread, here the refcount is decreased
                    producer->refs[received_msg->buffer_id].attached_to_client[received_msg->consumer_id] = 0;
                    set_buffer_status(next_out_ref, FREE, producer);
                }
                else
                {
                    VX_PRINT(VX_ZONE_ERROR, "PRODUCER %s: buffer ID not valid.\n", producer->name);
                }
            }
        }
        break;

        case VX_MSGTYPE_CONSUMER_CREATE_DONE: // consumer notifys about graph creation being completed
        {
            VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: received VX_GW_STATUS_CONSUMER_CREATE_DONE state from consumer %d \n", producer->name, received_msg->consumer_id);
            producer->consumers_list[received_msg->consumer_id].state = PROD_STATE_CLI_GRAPH_VERIFIED; 
        }
        break;

        default:
            VX_PRINT(
                VX_ZONE_ERROR,
                "PRODUCER %s: Received [UNKNOWN MESSAGE] %d\n",
                producer->name,
                received_msg->msg_type);
        break;
    }
}

static vx_bool check_ippc_clients_connected(vx_producer producer)
{
    // if one of the receiver is ready, register it and the sender can start sending data
    vx_bool new_client_connected = vx_false_e;
    for (vx_uint32 i = 0U; i < VX_GW_NUM_CLIENTS; i++)
    {
        if (E_IPPC_OK == ippc_sender_receiver_ready(&producer->m_sender_ctx.m_sender, i) && 
            (producer->consumers_list[i].state == PROD_STATE_CLI_NOT_CONNECTED))
        {
            producer->consumers_list[i].state       = PROD_STATE_CLI_CONNECTED;
            producer->consumers_list[i].consumer_id = i;
            producer->nb_consumers++;
            new_client_connected = vx_true_e;
        }
    
        if (producer->consumers_list[i].state == PROD_STATE_CLI_CONNECTED)
        {
            EIppcStatus l_status;

            VX_PRINT(
                VX_ZONE_INFO,
                "PRODUCER %s: send buffer metadata for consumer %u \n", producer->name, i);

            // set up backchannel context
            producer->consumers_list[i].m_receiver_ctx.m_port_map = producer->ippc_port[i + 1U];
            producer->consumers_list[i].m_receiver_ctx.m_msg_size = sizeof(vx_cons_msg_content_t);
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
                    VX_GW_NUM_CLIENTS);
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
                        "PRODUCER %s: consumer %u backchannel is ready, going to RUNNING state %u \n", producer->name, i);
                }
                EIppcStatus l_status;
                pthread_mutex_lock(&producer->client_mutex);
                vx_prod_msg_content_t* buffid_message = ippc_shem_payload_pointer(&producer->m_sender_ctx, sizeof(vx_prod_msg_content_t), &l_status);
                fill_reference_info(producer, buffid_message); 
                buffid_message->buffer_id        = -1;
                buffid_message->metadata_valid   = 0;
                buffid_message->last_buffer      = producer->last_buffer;
                buffid_message->metadata_size    = VX_GW_MAX_META_SIZE;
                send_id_message_consumers(producer, buffid_message, NULL);
                pthread_mutex_unlock(&producer->client_mutex);
            }
        }
    }
    return new_client_connected;
}

void* producer_connection_check_thread(void* arg)
{
    vx_producer producer = (vx_producer)arg;
    char threadname[280U];
    snprintf(threadname, 280U, "producer_conn_check_thread_%s", producer->name);
    pthread_setname_np(pthread_self(), threadname);

    VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: starting connection check thread \n", producer->name);
    while(vx_false_e == producer->connection_check_polling_exit) // assume that after first dequeue, frequent polling for clients is no longer necessary
    {
        pthread_mutex_lock(&producer->client_mutex);
        (void)check_ippc_clients_connected(producer);
        pthread_mutex_unlock(&producer->client_mutex);
        tivxTaskWaitMsecs(producer->connection_check_polling_time);
    }
    VX_PRINT(VX_ZONE_INFO, "PRODUCER %s: exiting connection check thread \n", producer->name);

    return NULL;
}

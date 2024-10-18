#include <stdio.h>
#include <string.h>
#include <ippc_shem.h>

vx_status ippc_shmem_init(const char* const f_shmem_name, const uint32_t f_chunk_number, 
                            const uint32_t f_broadcast_msgsize, const uint32_t f_unicast_msgsize, 
                            SIppcShmemContext* f_shmem)
vx_status ippc_shmem_init(vx_producer producer)
{
    EIppcStatus status;
    vx_status   vx_status_ret = (vx_status)VX_SUCCESS;
    size_t shm_size = 0UL;

    //calculate size of the necessary shared memory
    //size of registry meta data
    shm_size += IPPC_SHM_HANDLER_GET_REGISTRY_SIZE(producer->shem_ctx.m_num_ports);
    //size of the port system meta data (sendermeta + 1 receivermeta + buffermeta + chunk_count * chunk_size)
    shm_size += IPPC_SHM_HANDLER_GET_SIZE(producer->internals.pipeline_depth, sizeof(consumer_generic_payload_t), producer->shem_ctx.m_num_ports+1U);
    shm_size += IPPC_SHM_HANDLER_GET_SIZE(producer->internals.pipeline_depth, sizeof(producer_payload_t), producer->shem_ctx.m_num_ports+1U);
    //size of the sync objects
    shm_size += IPPC_SHM_HANDLER_GET_SYNC_SIZE(fproducer->internals.max_consumers*2);

    printf("Creating shared memory with size: %lu bytes\n", shm_size);

    status = ippc_shm_create(&producer->shem_ctx.m_shmem_handler,
                             &producer->shem_ctx.m_registry,
                             producer->internals.access_point_name,
                             shm_size,
                             producer->internals.max_consumers+1U);

    if (E_IPPC_OK == status)
    {
        //create one sync object for each receiver - we need N*2 for bi-direction comms
        status = ippc_shm_handler_create_sync(&producer->shem_ctx.m_shmem_handler, producer->shem_ctx.m_num_receivers*2U);
        if (E_IPPC_OK != status)
        {
            printf("Failed to create syncs for shmem\n");
            vx_status_ret = (vx_status)VX_FAILURE
        }

        printf("Msg size broadcast: %lu, unicast: %lu\n", f_shmem->m_msgsize_broadcast, f_shmem->m_msgsize_unicast);
        printf("Number of ports: %u\n", f_shmem->m_num_ports);

        for (uint32_t i = 0; i < (producer->shem_ctx.m_num_ports+1U); i++)
        {
            producer->shem_ctx.m_ports[i].m_port_id = g_ippc_port_configuration[i].m_port_id;
            producer->shem_ctx.m_ports[i].m_port_type = g_ippc_port_configuration[i].m_port_type;
            producer->shem_ctx.m_ports[i].m_receiver_index = g_ippc_port_configuration[i].m_receiver_index;

            if (PORT_TYPE_BROADCAST == g_ippc_port_configuration[i].m_port_type)
            {
                // forward/broadcast channel single port ID, 1->N port
                status = ippc_shm_handler_create(&f_shmem->m_shmem_handler,
                                                g_ippc_port_configuration[0].m_port_id,
                                                f_broadcast_msgsize,
                                                f_chunk_number,
                                                f_shmem->m_num_receivers);

                printf("creating port %u for fwd channel\n", g_ippc_port_configuration[0].m_port_id);
            }
            else
            {
                // 1->1 port for backchannel
                status = ippc_shm_handler_create(&f_shmem->m_shmem_handler,
                                                g_ippc_port_configuration[i].m_port_id,
                                                f_unicast_msgsize,
                                                f_chunk_number,
                                                1);
                printf("creating backchannel port %u, with recv index %u for fwd channel \n", g_ippc_port_configuration[i].m_port_id, g_ippc_port_configuration[i].m_receiver_index);
            }

            if (E_IPPC_OK != status)
            {
                printf("cant create shmem handle for forward port\n");
                vx_status_ret = (vx_status)VX_FAILURE;
                break;
            }
        }

        //registry has to be finalized before it can be used; no modifying after this
        ippc_registry_finalize(&f_shmem->m_registry);
    }
    else
    {
        printf("Failed to create shared memory!\n");
        vx_status_ret = (vx_status)VX_FAILURE;
    }

    return vx_status;
}

EIppcStatus ippc_shem_payload_pointer(ippc_sender_context_t* const f_sender_context, const size_t f_payload_size, void** f_payload)
{
    EIppcStatus status = E_IPPC_FAIL;

    void* send_msg = ippc_sender_reserve(&f_sender_context->m_sender);
    if (NULL != send_msg && f_payload_size == f_sender_context->m_msg_size)
    {
        *f_payload = send_msg;
        status = E_IPPC_OK;
    }
    return status;    
}

EIppcStatus ippc_shem_send(ippc_sender_context_t* const f_sender_context)
{
    ippc_sender_deliver(&f_sender_context->m_sender);

    if (PORT_TYPE_BROADCAST == f_sender_context->m_port_map.m_port_type)
    {
        // trigger all receivers
        for(uint32_t i = 0; i < f_sender_context->m_num_receivers; i++)
        {
            // todo internall track number of receivers
            status = ippc_shm_sync_trigger(f_sender_context->m_sync[i]);
            if (E_IPPC_OK != status)
            {
                status = E_IPPC_OK;//remov
                //break;
            }
        }
    }
    else
    {
        // trigger single receiver; will be at first slot
        status = ippc_shm_sync_trigger(f_sender_context->m_sync[0]);
    }
}
return status;
}
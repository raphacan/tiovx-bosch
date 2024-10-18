#include <vx_internal.h>


static vx_status ownDestructProducer(vx_reference ref);
static void producer_receiving_thread();

static void check_ippc_clients_connected(vx_producer producer)
{
      //if one of the receiver is ready, register it and the sender can start sending data
    for (uint32_t i = 0U; i < producer->internals.max_consumers; i++)
    {
        if ((vx_status)VX_SUCCESS == ippc_sender_receiver_ready(&sender->m_sender, i) && 
            (producer->internals.consumer_list[i].m_state == NOT_CONNECTED))
        {
            producer->internals.consumer_list[i].m_state = INIT;
            producer->internals.nb_receiver_ready++;
            VX_PRINT(VX_ZONE_ERROR, "new incoming consumer %u is ready, total number of consumers %u \n", i, nb_receiver_ready);
        }
    }     
}

static int32_t fill_vxreference_meta(vx_producer* producer, ovx_buffer_meta_payload_t* payload)
{
    vx_enum ref_type;
    vx_status status = VX_SUCCESS;
    payload->num_items      = 0;
    payload->item_index     = 0;
    payload->last_reference = 0; /* not sure if we want to keep it, we will transfer all the ref at the same time */
    
    
    status = vxQueryReference(producer->ref_to_export.refs_list[0], VX_REFERENCE_TYPE, &ref_type, sizeof(ref_type));
    if (framework_status != VX_SUCCESS)
    {
        VX_PRINT(VX_ZONE_ERROR, "PRODUCER: vxQueryReference() failed for object [%d]\n", i);
        break;
    }
    else if (ref_type == VX_TYPE_OBJECT_ARRAY)
    {    
        for (uint32_t i = 0; i < producer->ref_to_export.refs_list_size; i++) /* check if we want to export less buffer as populated ?*/
        {
            /* remove the "parent" input, this is part of the exported data 
               the first item is reserved for the obj array meta */
            rbvx_utils_export_ref_for_ipc_xfer_objarray(
                    producer->ref_to_export.refs_list[i],
                    &payload->num_items,
                    (tivx_utils_ref_ipc_msg_t*)&payload->ipc_message_item[0],
                    (tivx_utils_ref_ipc_msg_t*)&payload->ipc_message_item[1]);
        }
    }
    else
    {
        /* to be continued with single object */
    }
}

static void producer_broadcast_thread(const vx_producer producer)
{
    vx_reference dequeued_refs[OVXGW_MAX_NUM_REFS] = {0};
    vx_uint32    num_ready = 0;
    int l_stop_producer = g_stop_producer;
    producer_payload_t* payload = NULL;
    vy_reference replacement_ref_to_enqueue = NULL;

    while(shutdown == 0)
    {
        /* wait for graph to output a buffer */
        producer->streaming_cb.dequeue(producer->internals.graph, producer->ref_to_export.graph_parameter_index, 
                                       dequeued_refs, &num_ready);
        // buffer ID changes, e.g. we are dequeueing
        producer->internals.sequence_num++;
        // check if at least one receiver is available or if one has connected
        check_ippc_clients_connected(producer);
        if ((0 < producer->internals.nb_receiver_ready) && (true == get_new_buffer_from_pool(&replacement_ref_to_enqueue)))
        {
            /* enqueue the spare reference to keep the graph running */
            producer->streaming_cb.enqueue(replacement_ref_to_enqueue);
            /* fetch the payload pointer to write into */
            ippc_shem_payload_pointer(&producer->shem_sender_ctx, sizeof(producer_payload_t), (void*)&payload);
            /* write the generic payload: buffer id and supplementary */       
            payload->generic.ovx_buffer_index = get_buffer_id(dequeued_refs);
            size_t suppl_size = 0;
            producer->streaming_cb.meta_transmit(producer->internals.graph,
                                                 dequeued_refs[0],
                                                 &payload->generic.ovx_supplementary_data,
                                                 sizeof(payload->generic.ovx_supplementary_data)
                                                 &suppl_size);
            /*init new receiver if there is, by feeding the payload with the vx_reference meta information */
            for (uint32_t i = 0U; i < producer->internals.max_consumers; i++)
            {
                /* if one of the consumer is newly connected */
                if ((producer->internals.consumer_list[i].m_state ==INIT))
                {
                    VX_PRINT(VX_ZONE_INFO, "Producer - send buffer metadata for consumer: %u\n", i);
                    // set the meta via the export interface
                    fill_vxreference_meta(producer, &payload->m_ovx_buffer_meta);
#ifdef IPPC_SHEM_ENABLED
                    SIppcPortMap * port_map = ippc_get_port_by_recv_index(producer->shem_ctx, i);
                    payload->m_consumer_num = i; // index is enough to set up connection on the other side
                    payload->m_backchannel_port = port_map->m_port_id; // offset by 1
                    // set up backchannel context
                    producer->internals.consumer_list[i].m_consumer_num = i;
#endif
                    //launch backchannel thread, where we attach to the receiver of backchannel port
                    int thread_status = pthread_create(&producer->internals.consumer_list[i].m_thread, NULL, producer_bck_thread, (void*)producer);
                    if (thread_status != 0)
                    {
                        VX_PRINT(VX_ZONE_ERROR,"Failed to create backchannel thread for consumer %u\n", i);
                    }
                    else
                    {
                        producer->consumer_list[i].m_state = RUNNING;
                        VX_PRINT(VX_ZONE_INFO,"consumer %u backchannel is ready, going to RUNNING state\n", i);
                    }
                }
            }
            /* finalize the sending by broacasting the payload */
            vx_status status = ippc_shem_send(&producer->shem_sender_ctx);
            if ((vx_status)VX_SUCCESS != status)
            {
                VX_PRINT(VX_ZONE_ERROR, "Failed to send consumer message; enqueue bufer again\n");
                set_buffer_status(in_graph, &dequeued_refs);
                producer->streaming_cb.enqueue();
            }            
        }
        else
        {
            /* return directly the buffer into the producer*/
            producer->streaming_cb.enqueue();           
        }
    }
}

static void producer_bck_thread(const vx_producer producer)
{
    EIppcStatus status;
    SIppcReceiverContext receiver_backchannel;
    SProducerBackChannelContext* ctx = (SProducerBackChannelContext*)arg;

    // receiver ready number corresponds to the number we need to look up in the global list here
    SIppcPortMap * port_map = ippc_get_port_by_recv_index(ctx->m_shmem_ctx, ctx->m_consumer_num);
    printf("starting backchannel worker for consumer %u on port %u\n", ctx->m_consumer_num, port_map->m_port_id);

    status = ippc_start_receiver(ctx->m_shmem_ctx, port_map, producer_msg_handler, ctx, &receiver_backchannel);
    if ((vx_status)VX_SUCCESS == status)
    {
        VX_PRINT(VX_ZONE_INFO, "backchannel receiver attached to backchannel port\n");
    }
    else
    {
        VX_PRINT(VX_ZONE_ERROR, "failed to attach backchannel reciver, closing... \n");
        return NULL;
    }

    while(g_stop_producer || ctx->m_state != STOPPED)
    {
        // wait for message on backchannel
        ippc_receive(&receiver_backchannel, &payload);
        
    }

    printf("producer backchannel exiting for consumer %u\n", ctx->m_consumer_num);
}

static vx_status ownDestructProducer(vx_reference ref)
{
    vx_producer producer = vxCastRefAsProducer(ref, NULL); 
    ippc_shmem_deinit(producer->shmem_ctx, producer->internals.access_point_name);

    return (ownReleaseReferenceInt(
        &ref, (vx_enum)VX_TYPE_PRODUCER, (vx_enum)VX_EXTERNAL, NULL));
    
}

VX_API_ENTRY vx_status VX_API_CALL vxReleaseProducer(vx_producer *producer)
{
    return (ownReleaseReferenceInt(
        vxCastRefFromProducerP(producer), VX_TYPE_PRODUCER, (vx_enum)VX_EXTERNAL, NULL));
}

VX_API_ENTRY vx_status VX_API_CALL vxProducerAttachGraphParameter( const vx_producer  producer, const vx_graph_parameter_queue_params_t input_refs)
{
    /* fill here the graph parameters */
}


VX_API_ENTRY vx_producer VX_API_CALL vxCreateProducer(vx_graph graph, const vx_producer_params_t* params)
{
    vx_producer producer = NULL;
    vx_reference ref = NULL;
    vx_context context = vxGetContext(graph);
    vx_status status = (vx_status)VX_SUCCESS;
    /* create a producer object */
    ref = ownCreateReference(context, (vx_enum)VX_TYPE_PRODUCER, (vx_enum)VX_EXTERNAL, &context->base);
    if ((vxGetStatus(ref) == (vx_status)VX_SUCCESS) &&
        (ref->type == (vx_enum)VX_TYPE_PRODUCER))
    {
        /* status set to NULL due to preceding type check */
        producer = vxCastRefAsProducer(ref,NULL); 
        producer->base.destructor_callback = &ownDestructProducer; /* specific destructor because of no tiovx_obj*/
        producer->base.release_callback =    &ownReleaseReferenceBufferGeneric;
        (void)snprintf(producer->internals.name, VX_MAX_PRODUCER_NAME, params->name);
        (void)snprintf(producer->internals.access_point_name , VX_MAX_ACCESS_POINT_NAME, params->access_point_name);
        if (params->max_consumers > MAX_NB_OF_CONSUMERS)
        {
            VX_PRINT(VX_ZONE_WARNING, "IPPC SHEM cannot handle more than %d \n", MAX_NB_OF_CONSUMERS);
            producer->internals.max_consumers = MAX_NB_OF_CONSUMERS;
        }
        else
        {
            producer->internals.max_consumers = params->max_consumers;
        }
        for (uint32_t i = 0U; i < producer->internals.max_consumers; i++)
        {
            producer->internals.consumer_list[i] = NOT_CONNECTED;
        }        
        producer->internals.graph = graph;
        producer->internals.sequence_num = 0;
        producer->internals.total_sequences = 0;
#ifdef IPPC_SHEM_ENABLED
        status = ippc_shmem_init(producer->internals.access_point_name, poducer->internals.nb_of_buffers, 
                        sizeof(producer_payload_t), sizeof(consumer_generic_payload_t), &producer->shmem_ctx);
        status = ippc_start_sender(&(producer->shem_ctx), ippc_get_broadcast_port(&producer->shem_ctx, 0), 
                                   &(producer->shem_sender_ctx));
#endif

        if(status!=(vx_status)VX_SUCCESS)
        {
            status = vxReleaseProducer(&producer);
            if((vx_status)VX_SUCCESS != status)
            {
                VX_PRINT(VX_ZONE_ERROR, "Failed to release reference to a producer \n");
            }

            VX_PRINT(VX_ZONE_ERROR, "Could not create producer\n");
            ref = ownGetErrorObject(context, (vx_status)VX_ERROR_NO_RESOURCES);
            /* status set to NULL due to preceding type check */
            producer = vxCastRefAsProducer(ref, NULL);
        }
    }
    /* return the producer object */
    return(producer);
}

VX_API_ENTRY void VX_API_CALL vxRegisterConsumerConnectionNotifications(
      const vx_producer               producer,
      vx_producer_connect_notify_f    on_consumer_connection_cb,
      vx_producer_disconnect_notify_f on_consumer_disconnect_cb)
{
    return ((vx_status)VX_SUCCESS);
}

VX_API_ENTRY vx_status VX_API_CALL vxProducerStart(vx_producer producer)
{
    /* start the ippc broadcasting thread */
    int thread_status = pthread_create(&producer->internals.broadcast_thread, NULL, producer_broadcast_thread, (void*)producer);
    return ((vx_status)thread_status);
}

VX_API_ENTRY vx_status VX_API_CALL vxProducerShutdown(vx_producer producer, vx_uint32 max_timeout)
{
    return ((vx_status)VX_SUCCESS);
}

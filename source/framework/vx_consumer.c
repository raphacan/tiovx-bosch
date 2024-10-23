#include <vx_internal.h>
#include <vx_consumer.h>

static vx_status ownDestructConsumer(vx_reference ref)
{
    return ((vx_status)VX_SUCCESS); 
}

VX_API_ENTRY vx_status VX_API_CALL vxReleaseConsumer(vx_consumer* consumer)
{
    return (ownReleaseReferenceInt(
        vxCastRefFromConsumerP(consumer), VX_TYPE_CONSUMER, (vx_enum)VX_EXTERNAL, NULL));
}

VX_API_ENTRY vx_status VX_API_CALL vxConsumerAttachGraphParameter( const vx_consumer  consumer, const vx_graph_parameter_queue_params_t input_refs)
{
    /* fill here the graph parameters */
    return ((vx_status)VX_SUCCESS);
}

static void* consumer_receiver_thread(void* arg)
{
    return NULL;
}

VX_API_ENTRY vx_consumer VX_API_CALL vxCreateConsumer(vx_graph graph, const vx_consumer_params_t* params)
{
    vx_consumer consumer = NULL;
    vx_reference ref = NULL;
    vx_context context = vxGetContext((vx_reference)graph);
    vx_status status = (vx_status)VX_SUCCESS;
    /* create a consumer object */
    ref = ownCreateReference(context, (vx_enum)VX_TYPE_CONSUMER, (vx_enum)VX_EXTERNAL, &context->base);
    if ((vxGetStatus(ref) == (vx_status)VX_SUCCESS) &&
        (ref->type == (vx_enum)VX_TYPE_CONSUMER))
    {
        /* status set to NULL due to preceding type check */
        consumer = vxCastRefAsConsumer(ref,NULL); 
        consumer->base.destructor_callback = &ownDestructConsumer; /* specific destructor because of no tiovx_obj*/
        consumer->base.release_callback    = &ownReleaseReferenceBufferGeneric;
        (void)snprintf(consumer->internals.name, VX_MAX_CONSUMER_NAME, params->name);
        (void)snprintf(consumer->internals.access_point_name , VX_MAX_ACCESS_POINT_NAME, params->access_point_name);      
        consumer->internals.graph = graph;

        if(status!=(vx_status)VX_SUCCESS)
        {
            status = vxReleaseConsumer(&consumer);
            if((vx_status)VX_SUCCESS != status)
            {
                VX_PRINT(VX_ZONE_ERROR, "Failed to release reference to a consumer \n");
            }

            VX_PRINT(VX_ZONE_ERROR, "Could not create consumer\n");
            ref = ownGetErrorObject(context, (vx_status)VX_ERROR_NO_RESOURCES);
            /* status set to NULL due to preceding type check */
            consumer = vxCastRefAsConsumer(ref, NULL);
        }
    }

    /* return the consumer object */
    return(consumer);
}

VX_API_ENTRY vx_status VX_API_CALL vxConsumerStart(vx_consumer consumer)
{
    /* start the ippc broadcasting thread */
    int thread_status = pthread_create(&consumer->internals.receiver_thread, NULL, consumer_receiver_thread, (void*)consumer);
    return ((vx_status)thread_status);
}

VX_API_ENTRY vx_status VX_API_CALL vxConsumerShutdown(vx_consumer consumer, vx_uint32 max_timeout)
{
    return ((vx_status)VX_SUCCESS);
}

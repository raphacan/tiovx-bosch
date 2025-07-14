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

static vx_status ownInitConsumerObject(vx_consumer consumer, const vx_consumer_params_t* params);
static vx_status ownDestructConsumer(vx_reference ref);
static vx_status ownAllocConsumerBuffer(vx_reference ref);

vx_int32 getBufferIdForConsumer(vx_reference buffer_ref, vx_reference* reference_array, vx_uint32 reference_array_size)
{
    vx_int32 buffer_id;
    for (buffer_id = 0; (vx_uint32)buffer_id < reference_array_size; buffer_id++)
    {
        if (reference_array[buffer_id] == buffer_ref)
        {
            VX_PRINT(VX_ZONE_REFERENCE, "CONSUMER: found a buffer ref %p at index %d \n", buffer_ref, buffer_id);
            break;
        }
    }

    if ((vx_uint32)buffer_id == reference_array_size)
    {
        buffer_id = -1;
        VX_PRINT(
            VX_ZONE_ERROR,
            "CONSUMER: Dequeued reference cannot be found in the consumer registered references %s",
            "\n");
    }

    return buffer_id;
}

static vx_status ownDestructConsumer(vx_reference ref)
{
    return ((vx_status)VX_SUCCESS);
}

static vx_status ownAllocConsumerBuffer(vx_reference ref)
{
    return ((vx_status)VX_SUCCESS);
}

static vx_status ownInitConsumerObject(vx_consumer consumer, const vx_consumer_params_t* params)
{
    vx_status status = (vx_status)VX_SUCCESS;

    (void)snprintf(consumer->cons_base.name, VX_MAX_CONSUMER_NAME, params->name);
    (void)snprintf(consumer->cons_base.access_point_name , VX_MAX_ACCESS_POINT_NAME, params->access_point_name);
    consumer->cons_base.last_buffer               = 0;
    consumer->cons_base.last_buffer_dropped       = 0;
    consumer->cons_base.last_buffer_transmitted   = 0;
    consumer->cons_base.init_done                 = vx_false_e;
    consumer->cons_base.ref_import_done           = vx_false_e;
    consumer->cons_base.num_failures              = 0;
    consumer->cons_base.graph_obj                 = params->graph_obj;
    consumer->cons_base.create_graph_callback     = params->create_graph_callback;
    consumer->cons_base.enqueue_callback          = params->enqueue_callback;
    consumer->cons_base.dequeue_callback          = params->dequeue_callback;
    consumer->cons_base.receive_metadata_callback = params->receive_metadata_callback;
    consumer->cons_base.recovery_callback         = params->recovery_callback;
    consumer->cons_base.consumer_id               = params->consumer_id;
    consumer->cons_base.connect_polling_time      = params->connect_polling_time;
    consumer->cons_base.state                     = VX_CONS_STATE_DISCONNECTED;    

    pthread_mutexattr_t buffInfoMutexAttr;
    pthread_mutexattr_init(&buffInfoMutexAttr);

    status = pthread_mutex_init(&consumer->cons_base.buffer_mutex, &buffInfoMutexAttr);
    if (status != VX_SUCCESS)
    {
        VX_PRINT(VX_ZONE_ERROR, "CONSUMER: pthread_mutex_init() failed for buffer info mutex\n");
        return (vx_status)VX_FAILURE;
    }
#if defined(QNX)
    status = ownInitConsumerObjectIppc(consumer, params);
#endif

    return status;
}

VX_API_ENTRY vx_status VX_API_CALL vxReleaseConsumer(vx_consumer* consumer)
{
    vx_consumer this_consumer = consumer[0];
    this_consumer->cons_base.state = VX_CONS_STATE_FLUSH;
    pthread_join(this_consumer->cons_base.receiver_thread, NULL);
    pthread_join(this_consumer->cons_base.backchannel_thread, NULL);
    return (ownReleaseReferenceInt(
        vxCastRefFromConsumerP(consumer), VX_TYPE_CONSUMER, (vx_enum)VX_EXTERNAL, NULL));
}

VX_API_ENTRY vx_consumer VX_API_CALL vxCreateConsumer(vx_context context, const vx_consumer_params_t* params)
{
    vx_consumer consumer = NULL;
    vx_reference ref = NULL;
    vx_status status = (vx_status)VX_SUCCESS;
    /* create a consumer object */
    ref = ownCreateReference(context, (vx_enum)VX_TYPE_CONSUMER, (vx_enum)VX_EXTERNAL, &context->base);
    if ((vxGetStatus(ref) == (vx_status)VX_SUCCESS) &&
        (ref->type == (vx_enum)VX_TYPE_CONSUMER))
    {
        /* status set to NULL due to preceding type check */
        consumer = vxCastRefAsConsumer(ref,NULL); 
        consumer->cons_base.ref_base.destructor_callback = &ownDestructConsumer; /* specific destructor because of no tiovx_obj*/
        consumer->cons_base.ref_base.mem_alloc_callback  = &ownAllocConsumerBuffer;
        consumer->cons_base.ref_base.release_callback    = &ownReleaseReferenceBufferGeneric;
        consumer->cons_base.context = context;

        status = ownInitConsumerObject(consumer, params);
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
    vx_status status = (vx_status)VX_SUCCESS;
#if defined(LINUX)
    status = consumerStartSock(consumer);
#elif defined(QNX)
    status = consumerStartIppc(consumer);
#endif
    return(status);
}

VX_API_ENTRY vx_uint32 VX_API_CALL vxConsumerShutdownStatus(vx_consumer consumer)
{
    return consumer->cons_base.last_buffer_transmitted;
}

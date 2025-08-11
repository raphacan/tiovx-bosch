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

static vx_status ownInitProducerObject(vx_producer producer, const vx_producer_params_t* params);
vx_status setBufferStatus(vx_int32 buffer_id, producer_buffer_stat_e curr_status, vx_producer producer);
vx_uint8 getNumBufferWithStatus(vx_producer producer, producer_buffer_stat_e status);
vx_int32 getBufferIdForProducer(vx_reference current_ref, vx_producer producer);
vx_uint32 getNumLockedFramesByClient(vx_producer producer, vx_uint32 client);
void updateLockedState(vx_producer producer);
static vx_status ownDestructProducer(vx_reference ref);
static vx_status ownAllocProducerBuffer(vx_reference ref);

vx_status setBufferStatus(vx_int32 buffer_id, producer_buffer_stat_e curr_status, vx_producer producer)
{
    vx_bool     found                = vx_false_e;
    vx_bool     forbidden_transition = vx_false_e;
    const char* state2string[]       = {"IN_GRAPH", "LOCKED", "FREE"};

    if(-1 < buffer_id)
    {
        pthread_mutex_lock(&producer->prod_base.buffer_mutex);

        producer_buffer_stat_e old_status = producer->prod_base.refs[buffer_id].buffer_status;
        found                             = vx_true_e;

        switch(old_status)
        {
            case IN_GRAPH:
            {
                if(LOCKED == curr_status)
                {
                    // IN_GRAPH -> LOCKED: after leaving producer graph
                    producer->prod_base.refs[buffer_id].refcount++;
                    producer->prod_base.refs[buffer_id].buffer_status = curr_status;
                }
                else if(FREE == curr_status)
                {
                /*
                    * IN_GRAPH -> FREE: dequeue from graph in wait state OR
                    * it could happen that we have a double enqueue due to a miscommunication, where a buffer is freed by 
                    * a consumer that was not supposed to free it (buffer message overwrite while consumer whas processing it)
                    * to prevent that, query the number of enqueues for the reference, only enqueue if not done already                
                    * enqueue reference into graph from here, do not change its status 
                    *
                    * Example Usecase: 
                    * 1. consumer gets new message (e.g. bufID 0, mask 1)
                    * 2. consumer does time consuming copy of supplementary (during that time, producer overwrites with (e.g. bufID 1, mask 0)
                    * 3. consumer enqueues bufID 1 (although it shouldn't but it doesn't re evaluate mask flag)
                    * 4. consumer releases bufID 1 which producer does not expect to be released from that consumer, 
                    * while it DOESNT release bufID 0 although producer expects that.
                    */ 

                    vx_uint32 num_enqueues = 0;
                    vx_status query_status = vxQueryReference(producer->prod_base.refs[buffer_id].ovx_ref, VX_REFERENCE_ENQUEUE_COUNT, &num_enqueues, sizeof(num_enqueues));
                    if (query_status == VX_SUCCESS)
                    {
                        if (num_enqueues > 0)
                        {
                            VX_PRINT(VX_ZONE_WARNING, "reference has been enqueued back to the graph already \n"); 
                        }
                        else
                        {
                            producer->prod_base.enqueue_callback(producer->prod_base.graph_obj, producer->prod_base.refs[buffer_id].ovx_ref);
                            producer->prod_base.nbEnqueueFrames++;
                        }
                    }
                    else
                    {
                        VX_PRINT(VX_ZONE_ERROR, "Failed to query the number of enqueues for reference with id %d\n", buffer_id);
                    }
                }
                else
                {
                    // IN_GRAPH -> IN_GRAPH: this is handled implicitly in handle_producer_graph
                    forbidden_transition = vx_true_e;
                }
            }
            break;

            case LOCKED:
            {
                if(LOCKED == curr_status)
                {
                    // LOCKED -> LOCKED: after being sent to more consumers
                    producer->prod_base.refs[buffer_id].refcount++;
                    // base the locked count on the latest transmission therefore reset from here
                    VX_PRINT(VX_ZONE_INFO, "reset locked count for reference with id %d \n", buffer_id); 
                    producer->prod_base.refs[buffer_id].locked_count = 0;
                }
                else if(FREE == curr_status)
                {
                    // LOCKED -> FREE: after coming back from consumer or consumer timeout
                    producer->prod_base.refs[buffer_id].refcount--;
                    if (producer->prod_base.refs[buffer_id].refcount == 0)
                    {
                        producer->prod_base.refs[buffer_id].buffer_status = IN_GRAPH;
                        // enqueue reference into graph from here
                        producer->prod_base.enqueue_callback(producer->prod_base.graph_obj, producer->prod_base.refs[buffer_id].ovx_ref);
                        producer->prod_base.nbEnqueueFrames++;

                        VX_PRINT(VX_ZONE_INFO, "enqueued back and reset locked count for reference with id %d \n", buffer_id); 
                        producer->prod_base.refs[buffer_id].locked_count = 0;
                    }
                }
                else
                {
                    // LOCKED -> IN_GRAPH
                    forbidden_transition = vx_true_e;
                }
            }
            break;

            case FREE:
            {
                if(IN_GRAPH == curr_status)
                {
                    // FREE -> IN_GRAPH: enqueueing a fresh ref into producer
                    producer->prod_base.refs[buffer_id].buffer_status = curr_status;   
                }
                else if(LOCKED == curr_status)
                {
                    // FREE -> LOCKED: possible if the locked buffer is freed before the transmission to second consumer is
                    // shutdown, still we need to increase refcount to prevent buffer handling problems
                    producer->prod_base.refs[buffer_id].refcount++;
                    producer->prod_base.refs[buffer_id].buffer_status = curr_status;
                }
                else
                {
                    // FREE -> FREE
                    forbidden_transition = vx_true_e;
                }
            }
            break;

            default:
            {
                VX_PRINT(VX_ZONE_ERROR, "setBufferStatus: Invalid Buffer status %s", "\n");
            }
        }


        if(vx_true_e == forbidden_transition)
        {
            // fatal error; state transition not allowed; should never get here
            VX_PRINT(
                VX_ZONE_ERROR,
                "PRODUCER: Reference state transition (%s -> %s) not allowed for buffer %d!%s",
                state2string[old_status],
                state2string[curr_status],
                buffer_id,
                "\n");
        }

        if (producer->prod_base.refs[buffer_id].buffer_status != old_status)
        {
            uint64_t currentTime = tivxPlatformGetTimeInUsecs();
            VX_PRINT(
                VX_ZONE_INFO,
                "PRODUCER: buffer %d found, status changed from %s to %s, refcount is %d \n",
                buffer_id,
                state2string[old_status],
                state2string[curr_status],
                producer->prod_base.refs[buffer_id].refcount);
            VX_PRINT(
                VX_ZONE_INFO,
                "PRODUCER: reference was in state %s for %llu usecs\n",
                state2string[old_status],
                currentTime - producer->prod_base.refs[buffer_id].state_timestamp);
            producer->prod_base.refs[buffer_id].state_timestamp = currentTime;
        }
        else
        {
            VX_PRINT(
                VX_ZONE_INFO,
                "PRODUCER: buffer %d found, status unchanged (%s), refcount is %d\n",
                buffer_id,
                state2string[old_status],
                producer->prod_base.refs[buffer_id].refcount);
        }

        pthread_mutex_unlock(&producer->prod_base.buffer_mutex);
    }

    if ((found == vx_true_e) && (forbidden_transition == vx_false_e))
    {
        return VX_SUCCESS;
    }
    else
    {
        return VX_FAILURE;
    }
}

vx_uint8 getNumBufferWithStatus(vx_producer producer, producer_buffer_stat_e status)
{
    vx_uint32     buffer_id;
    vx_uint8     num_buffers_found = 0;

    for (buffer_id = 0; buffer_id < producer->prod_base.num_buffers; buffer_id++)
    {
        if (status == producer->prod_base.refs[buffer_id].buffer_status)
        {
            VX_PRINT(VX_ZONE_INFO, "PRODUCER found a buffer ref %p with status %d \n", producer->prod_base.refs[buffer_id].ovx_ref, status);
            num_buffers_found++;
        }
    }
    if (0U == num_buffers_found)
    {
        VX_PRINT(VX_ZONE_INFO, "PRODUCER no buffer found with status %d \n", status);
    }

    return num_buffers_found;
}

vx_int32 getBufferIdForProducer(vx_reference current_ref, vx_producer producer)
{
    vx_int32 buffer_id;
    for (buffer_id = 0; (vx_uint32)buffer_id < producer->prod_base.num_buffers; buffer_id++)
    {
        if (producer->prod_base.refs[buffer_id].ovx_ref == current_ref)
        {
            VX_PRINT(VX_ZONE_INFO, "PRODUCER found a buffer ref %p at index %d \n", current_ref, buffer_id);
            break;
        }
    }

    if ((vx_uint32)buffer_id == producer->prod_base.num_buffers)
    {
        buffer_id = -1;
        VX_PRINT(
            VX_ZONE_ERROR,
            "PRODUCER Dequeued reference cannot be found in the consumer registered references %s",
            "\n");
    }

    return buffer_id;
}

vx_uint32 getNumLockedFramesByClient(vx_producer producer, vx_uint32 client)
{
    vx_uint32 locked_cnt = 0;
    for (vx_uint32 i = 0; i < producer->prod_base.num_buffers; i++)
    {
        if (producer->prod_base.refs[i].attached_to_client[client] == 1)
            locked_cnt++;
    }
    return locked_cnt;
}

/* 
 * every cycle (everytime a new buffer is dequeued) loop through 
 * all locked buffers and increase locked count for each buffer. if a 
 * buffer's locked count reaches a threshold, assume that consumer  
 * has a problem and release buffer back to the producer
 */
void updateLockedState(vx_producer producer)
{
    vx_uint8     buffer_id;

    pthread_mutex_lock(&producer->prod_base.buffer_mutex);
    for (buffer_id = 0; buffer_id < producer->prod_base.num_buffers; buffer_id++ )
    {
        if (LOCKED == producer->prod_base.refs[buffer_id].buffer_status)
        {
            producer->prod_base.refs[buffer_id].locked_count++;
        }

        if(VX_GC_MAX_LOCKED_CNT == producer->prod_base.refs[buffer_id].locked_count)
        {
            for (vx_uint32 client_id = 0U; client_id < VX_GC_NUM_CLIENTS; client_id++)
            {
                VX_PRINT(VX_ZONE_WARNING, "detach a reference with id %d from client %d with current attach state %d \n", 
                                                buffer_id, client_id, producer->prod_base.refs[buffer_id].attached_to_client[client_id]); 
                producer->prod_base.refs[buffer_id].attached_to_client[client_id] = 0;
            }
            VX_PRINT(VX_ZONE_WARNING, "release a reference with id %d because it has been locked for too long \n", buffer_id);
            producer->prod_base.refs[buffer_id].locked_count = 0;
            producer->prod_base.refs[buffer_id].refcount = 0;            
            producer->prod_base.refs[buffer_id].buffer_status = IN_GRAPH;
            producer->prod_base.enqueue_callback(producer->prod_base.graph_obj, producer->prod_base.refs[buffer_id].ovx_ref);
            producer->prod_base.nbEnqueueFrames++;
        }
    }

    pthread_mutex_unlock(&producer->prod_base.buffer_mutex);
}

static vx_status ownInitProducerObject(vx_producer producer, const vx_producer_params_t* params)
{
    vx_status status = (vx_status)VX_SUCCESS;
    (void)snprintf(producer->prod_base.name, VX_MAX_PRODUCER_NAME, params->name);
    (void)snprintf(producer->prod_base.access_point_name , VX_MAX_ACCESS_POINT_NAME, params->access_point_name);

    producer->prod_base.graph_obj                 = params->graph_obj;
    producer->prod_base.num_buffers               = params->num_buffers;
    producer->prod_base.num_buffer_refs_export    = params->num_buffer_refs_export;
    producer->prod_base.max_refs_locked_by_client = params->max_refs_locked_by_client;
    producer->prod_base.dequeue_callback          = params->dequeue_callback;
    producer->prod_base.enqueue_callback          = params->enqueue_callback;
    producer->prod_base.transmit_meta             = params->transmit_meta;

    producer->prod_base.state                     = VX_PROD_STATE_INIT;
    producer->prod_base.nb_consumers              = 0;

    if (producer->prod_base.num_buffers < 2 || producer->prod_base.num_buffers > VX_GC_MAX_NUM_REFS)
    {
        VX_PRINT(VX_ZONE_ERROR, "PRODUCER: Bad number of producer references!\n");
        return (vx_status)VX_FAILURE;
    }

    for (vx_uint32 buff_id = 0; buff_id < producer->prod_base.num_buffer_refs_export; buff_id++)
    {
        if ((params->ref_to_export[buff_id] != NULL) &&
            (VX_SUCCESS == vxGetStatus(params->ref_to_export[buff_id])))
        {
            //  Copy the graph OUTPUT references to the producer internal buffer
            producer->prod_base.refs[buff_id].ovx_ref       = params->ref_to_export[buff_id];
            producer->prod_base.refs[buff_id].buffer_status = FREE;
            producer->prod_base.refs[buff_id].refcount      = 0;
        }
        else
        {
            VX_PRINT(VX_ZONE_ERROR, "PRODUCER: NULL pipeline references detected, producer_init failed\n");
            return (vx_status)VX_FAILURE;
        }
    }

    pthread_mutexattr_t buffInfoMutexAttr;
    pthread_mutexattr_init(&buffInfoMutexAttr);
    status = pthread_mutex_init(&producer->prod_base.buffer_mutex, &buffInfoMutexAttr);
    if (status != VX_SUCCESS)
    {
        VX_PRINT(VX_ZONE_ERROR, "PRODUCER: pthread_mutex_init() failed for buffer info mutex\n");
        return (vx_status)VX_FAILURE;
    }

    pthread_mutexattr_t client_mutexAttr;
    pthread_mutexattr_init(&client_mutexAttr);
    status = pthread_mutex_init(&producer->prod_base.client_mutex, &client_mutexAttr);
    if (status != VX_SUCCESS)
    {
        VX_PRINT(VX_ZONE_ERROR, "PRODUCER: pthread_mutex_init() failed for client handling mutex\n");
        return (vx_status)VX_FAILURE;
    }

#if defined(LINUX)
    status = ownInitProducerObjectSock(producer, params);
#elif defined(QNX)
    status = ownInitProducerObjectIppc(producer, params);
#endif
    return status;    
}

static vx_status ownDestructProducer(vx_reference ref)
{
    return ((vx_status)VX_SUCCESS);
}

static vx_status ownAllocProducerBuffer(vx_reference ref)
{
    return ((vx_status)VX_SUCCESS);
}

VX_API_ENTRY vx_producer VX_API_CALL vxCreateProducer(vx_context context, const vx_producer_params_t* params)
{
    vx_producer producer = NULL;
    vx_reference ref = NULL;
    vx_status status = (vx_status)VX_SUCCESS;
    /* create a producer object */
    if(ownIsValidContext(context) == (vx_bool)vx_true_e)
    {
        ref = ownCreateReference(context, (vx_enum)VX_TYPE_PRODUCER, (vx_enum)VX_EXTERNAL, &context->base);

        if ((vxGetStatus(ref) == (vx_status)VX_SUCCESS) &&
            (ref->type == (vx_enum)VX_TYPE_PRODUCER))
        {
            /* status set to NULL due to preceding type check */
            producer = vxCastRefAsProducer(ref,NULL);
            producer->prod_base.ref_base.destructor_callback = &ownDestructProducer; /* specific destructor because of no tiovx_obj*/
            producer->prod_base.ref_base.mem_alloc_callback  = &ownAllocProducerBuffer;
            producer->prod_base.ref_base.release_callback    = &ownReleaseReferenceBufferGeneric;

            status = ownInitProducerObject(producer, params);

            if((vx_status)VX_SUCCESS != status)
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
    }
    /* return the producer object */
    return(producer);
}

VX_API_ENTRY vx_status VX_API_CALL vxProducerStart(vx_producer producer)
{
    vx_status status = (vx_status)VX_SUCCESS;
#if defined(LINUX)
    status = producerStartSock(producer);
#elif defined(QNX)
    status = producerStartIppc(producer);
#endif
    return(status);
}

VX_API_ENTRY vx_status VX_API_CALL vxProducerShutdown(vx_producer producer)
{
    producer->prod_base.last_buffer = 1;
    return ((vx_status)VX_SUCCESS);
}

VX_API_ENTRY vx_status VX_API_CALL vxReleaseProducer(vx_producer* producer)
{
    vx_producer this_producer = producer[0];
#if defined(LINUX)
    releaseProducerSock(producer);
#elif defined(QNX)
    releaseProducerIppc(producer);
#endif
    pthread_mutex_destroy(&(this_producer->prod_base.buffer_mutex));
    pthread_mutex_destroy(&(this_producer->prod_base.client_mutex));
    return (ownReleaseReferenceInt(
        vxCastRefFromProducerP(producer), VX_TYPE_PRODUCER, (vx_enum)VX_EXTERNAL, NULL));
}

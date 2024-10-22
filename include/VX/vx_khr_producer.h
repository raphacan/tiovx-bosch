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

#ifndef _VX_KHR_PRODUCER_H_
#define _VX_KHR_PRODUCER_H_

/*!
 * \file
 * \brief The OpenVX producer extension API.
 */

#include <VX/vx.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*!
 * \file
 * \brief The OpenVX producer extension API.
 */

#define OPENVX_KHR_PRODUCER  "vx_khr_producer"

/*! \brief The object type enumeration for user data object.
 * \ingroup group_vx_producer
 */
#define VX_TYPE_PRODUCER 0x819

#define VX_MAX_PRODUCER_NAME 20

#define VX_MAX_ACCESS_POINT_NAME 20

    /**
     * \brief Custom callback function for dequeuing references from the producer graph.
     *
     * This function is called by the producer server thread waiting for new references to send to the consumer(s).
     *
     * \param [in] graph A pointer to the OpenVX graph object that the producer is using.
     * \param [in] graph_parameter_index The index of the graph parameter to dequeue.
     * \param [out] dequeued_refs An array of dequeued references.
     * \param [out] num_dequeued_refs The number of dequeued references.
     *
     * \return A status of the operation. If the dequeue is successful, the function will return VX_SUCCESS.
     *         if the returned status is not VX_SUCCESS, the producer will not send any buffer to the consumer(s)
     */
    typedef vx_status (*vxProducerDequeueCallback)(
        vx_graph     graph,
        vx_uint32    graph_parameter_index,
        vx_reference dequeued_refs[],
        vx_uint32*   num_dequeued_refs);

    /**
     * \brief Custom callback function to enqueue references into the producer graph.
     *
     * This function is called by the producer server thread when one (or more) buffer(s) has(have) been released by
     * (all) the consumer(s) and get it returned back to the producer graph.
     *
     * \param [in] graph A pointer to the OpenVX graph object that the producer is using.
     * \param [in] graph_parameter_index The index of the graph parameter to enqueue.
     * \param [in] enqueue_ref the reference(s) to enqueue
     * \param [in] num_enqueue_refs The number of references to enqueue
     *
     * \return A status of the operation. If the enqueue is successful, the function will return VX_SUCCESS.
     */
    typedef vx_status (*vxProducerEnqueueCallback)(
        vx_graph     graph,
        vx_uint32    graph_parameter_index,
        vx_reference enqueue_ref,
        vx_uint32    num_enqueue_refs);

    /**
     * \brief Custom callback function to forward meta datas (for eg. supplementary) to the consumer(s).
     *
     *
     * \param [in]  graph A pointer to the OpenVX graph object that the producer is using.
     * \param [in]  ref the reference which is being sent, usefull if you want to extract the supplementary data
     * \param [out] metadata The metadata array to be filled out by the application.
     + \param [in]  max_size maximum size accepted by the producer - consumer(s) communication
     * \param [out] size_t* size the size of the metadata array
     * \return if VX_SUCCESS is not returned, the producer will not consider the metadata as valid
     *         and nothing will be forwarded to the consumer(s)
     */
    typedef vx_status (*vxProducerTransmitMetadataCallback)(
        vx_graph     graph,
        vx_reference ref,
        vx_uint32    metadata[],
        size_t       max_size,
        size_t*      size);

    /**
     * \brief Custom callback function to notify the producer when a frame is dropped for a corresponding consumer.
     *
     * This function is called by the producer server thread when a frame is dropped for a corresponding consumer.
     * i.e. the number of allowed locked buffers by the consumer is exceeded.
     * example with two consumers running at different speed.
     * this threshold is shared by the consumer when connected to the producer and should correspond to its normal
     * behavior.
     *
     * \param [in] graph A pointer to the OpenVX graph object that the producer is using.
     * \param [in] consumer_id The unique identifier (PID) of the consumer who is retaining too much buffers.
     * \param [in] consumer_name The name of the consumer which is specified in the consumer parameters.
     * \param [in] dequeued_refs An array of references that were dequeued by the consumer and won't be send to the
     * corresponding consumer.
     * \param [in] num_dequeued_refs The number of dequeued references.
     *
     */
    typedef void (*vxProducerFrameDropCallback)(
        vx_graph        graph,
        const vx_uint32 consumer_id,
        const char*     consumer_name,
        vx_reference    dequeued_refs[],
        vx_uint32*      num_dequeued_refs);

    /**
     * \brief Callback function for notifying when a consumer connects to the producer.
     *
     * \param consumer_id The unique identifier (PID) of the consumer.
     * \param consumer_name The name of the consumer.
     *
     */
    typedef void (*vx_producer_connect_notify_f)(const vx_uint32 consumer_id, const char* consumer_name);

    /**
     * \brief Callback function for notifying when a consumer disconnects from the producer.
     *
     * \param consumer_id The unique identifier (PID) of the consumer.
     * \param consumer_name The name of the consumer.
     */
    typedef void (*vx_producer_disconnect_notify_f)(const vx_uint32 consumer_id, const char* consumer_name);

    typedef struct _vx_producer_params_t
    {
        char                               name[VX_MAX_PRODUCER_NAME];
        char                               access_point_name[VX_MAX_ACCESS_POINT_NAME];        
        vx_uint32                          max_consumers;
        vx_uint16                          pipeline_depth; 
    } vx_producer_params_t;

    typedef struct _vx_streaming_cb_t
    {
        vxProducerDequeueCallback          dequeue;
        vxProducerEnqueueCallback          enqueue;
        vxProducerTransmitMetadataCallback meta_transmit;
        vxProducerFrameDropCallback        frame_drop;
    } vx_streaming_cb_t;

    typedef struct _vx_notify_cb_t
    {
        vx_producer_connect_notify_f       notify_new_connect;
        vx_producer_disconnect_notify_f    notify_disconnect;
    }vx_notify_cb_t;  

    /**
     * \brief Creates a producer server for inter-process/inter SoC etc. communication.
     *
     * This function creates a producer server that can be used to share OpenVX objects between different consumers
     (graph).
     *
     * \param [in] name The name of the producer server.
     * \param [in] graph The OpenVX graph object that the producer server will use.
     * \param [in] access_point_name The name of the access point for the producer server.

     * \param [in] max_consumers The maximum number of consumers that can connect to the producer server.
     * \param [in] dequeue_callback The callback function that will be called when a buffer is dequeued.
     * \param [in] enqueue_callback The callback function that will be called when a buffer is enqueued.
     * \param [in] supplementary_callback The callback function that will be called to get supplementary metadata.
     * \param [in] framedrop_callback The callback function that will be called when a frame is dropped for the
     corresponding consumer
     *
     * \return this is returning an vx_producer object
     */
    VX_API_ENTRY vx_producer VX_API_CALL vxCreateProducer(vx_graph graph, const vx_producer_params_t* params);

    /**
     * \brief add the graph output where the producer server has to dequeue and enqueue
     *
     * \param [in] producer server object
     * \param [in] param_index parameter index define as output of the graph
     * \param [in] num_buffer_refs The number of buffer references that the producer server will export, shall
     * correspond to the number of buffers contained in the ref array. \param [in] ref[] array of references
     * corresponding to the graph parameter Remark: the number of buffer references corresponds to your buffer pool size
     * that you want to exchange with the consumer(s). The pool size should be big enough to be able to run the producer
     * and consumer(s) in parallel. So the pool size should be at least 2 (double buffering) or more depending on your
     * consumer(s) processing time. For system stability and performance, it is recommended to have a pool size of at
     * least 3 as you can have the producer and consumer(s) running in parallel and have a buffer in reserve.
     */
    VX_API_ENTRY vx_status VX_API_CALL vxProducerAttachGraphParameter(
        const vx_producer  producer,
        const vx_graph_parameter_queue_params_t input_refs);

    /**
     * \brief Registers callbacks functions called when a consumer disconnects from the producer.
     *        you can implement here your own error management and reporting in case of lost of communication with the
     * one of the consumers
     *
     * \param producer The producer object.
     * \param on_consumer_connection_cb The callback function called when a consumer connects.
     * \param on_consumer_disconnect_cb The callback function called when a consumer disconnects.
     *                                 notes: In that case the producer framework will stop sending buffers to the
     * disconnected consumer and will also release all the buffers that were used and locked by this consumer.
     *
     */
    VX_API_ENTRY vx_status VX_API_CALL vxRegisterConsumerConnectionNotifications(
        const vx_producer               producer,
        vx_producer_connect_notify_f    on_consumer_connection_cb,
        vx_producer_disconnect_notify_f on_consumer_disconnect_cb);

    /**
     * \brief Starts the producer server.
     *
     * This function starts the producer server and prepares it for accepting connections from consumers.
     * Until a consumer connects, the producer will not send any buffers and will enqueue them back to the producer
     * graph.
     *
     * \param producer A pointer to the producer server object.
     *
     * \return A status of the operation. If the operation is successful, the function will return VX_SUCCESS.
     * If the result is not VX_SUCCESS, the producer has not been started and the application should decide what to do
     * (retry, stop, etc.) returns VX_ERROR_NO_RESOURCES if the graph parameter and the reference were not given to the
     * producer
     */
    VX_API_ENTRY vx_status VX_API_CALL vxProducerStart(vx_producer producer);

    /**
     * \brief Shuts down the producer server.
     *
     * This function initiates the shutdown process for the producer server. It sends the last buffer to the consumer(s)
     * and waits for all consumer(s) pipelines to be flushed before stopping the producer. This is a blocking interface,
     * it will wait until all the consumers have finished their processing (and returned the last buffer to the producer
     * server) or will time out after a certain time configured in the producer. after this point all the exchanged
     * buffer can be released
     *
     * \param producer A pointer to the producer object.
     * \param max_timeout The maximum time to wait for all consumers to finish their processing and returned the last
     * buffer back.
     *
     * \return A status of the operation. If the shutdown is successful, the function will return VX_SUCCESS.
     * If the result is not VX_SUCCESS, the producer did not received all the buffers back from the consumer(s) and the
     * timeout has been reached. the application can still decide to release the buffer and stop the producer
     */
    VX_API_ENTRY vx_status VX_API_CALL vxProducerShutdown(vx_producer producer, vx_uint32 max_timeout);

    VX_API_ENTRY vx_status VX_API_CALL vxReleaseProducer(vx_producer* producer);
    
#ifdef __cplusplus
}
#endif

#endif // _VX_KHR_PRODUCER_H_

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

#ifndef VX_PRODUCER_H_
#define VX_PRODUCER_H_

#include <vx_reference.h>
#include <pthread.h>
#include <tivx_utils_ipc_ref_xfer.h>

/*! \brief The Producer state enumeration 
 * \ingroup group_vx_producer
 */
typedef enum
{
    VX_PROD_STATE_INIT  = 0x0,
    VX_PROD_STATE_RUN   = 0x1,
    VX_PROD_STATE_WAIT  = 0x2,
    VX_PROD_STATE_FLUSH = 0x3,
} producer_state_e;

/*! \brief The Producer-Consumer connection state enumeration 
 * \ingroup group_vx_producer
 */
typedef enum
{
    PROD_STATE_CLI_NOT_CONNECTED  = 0x0,
    PROD_STATE_CLI_CONNECTED      = 0x1,
    PROD_STATE_CLI_GRAPH_VERIFIED = 0x2,
    PROD_STATE_CLI_RUNNING        = 0x3,
    PROD_STATE_CLI_FLUSHED        = 0x4,
    PROD_STATE_CLI_FAILED         = 0x5
} producer_client_state_e;

/*! \brief The Producer buffer status enumeration 
 * \ingroup group_vx_producer
 */
typedef enum
{
    IN_GRAPH = 0x00,
    LOCKED   = 0x01,
    FREE     = 0x02
} producer_buffer_stat_e;

/*! \brief The metadata information sent by the producer
 * \ingroup group_vx_producer
 */
typedef struct
{
    /*! \brief flag set when metadata can be read by consumer */
    vx_uint8 is_valid;
    /*! \brief size of metadata */
    vx_size size;
} metadata_attr_t;

/*! \brief The buffer information exchanged b/w producer and consumer
 * \ingroup group_vx_producer
 */
typedef struct
{
    /*! \brief Indicates id of the buffer to be exchanged with the consumer */
    vx_int32  id;
    /*! \brief Indicates to receivers whether current frame shall be consumed or not */
    vx_uint32  mask;
    /*! \brief flag to indicate if this is the last reference to be exchanged with the consumer */
    vx_uint32 last_buffer;
    /*! \brief flag to inform consumer whether previous frame has been dropped by producer */
    vx_uint8 last_frame_dropped;
    /*! \brief The metadata information sent by the producer */
    metadata_attr_t metadata;
    /*! \brief number of total object array items; set to zero if reference is not object array */
    vx_uint8 num_items;    
} buffer_info_t;

/*! \brief The Producer buffer information
 * \ingroup group_vx_producer
 */
typedef struct
{
    /*! \brief Producer reference */
    vx_reference           ovx_ref;
    /*! \brief Number of consumers locking the reference */
    vx_int32               refcount;

    /*! \brief Status of reference in producer */
    producer_buffer_stat_e buffer_status;
    /*! \brief flag to indicate whether the producer is connected to the consumer */
    vx_uint8               attached_to_client[VX_GC_NUM_CLIENTS];

    /*! \brief Indicates the time at which buffer status was set */
    vx_uint64              state_timestamp;

    /*! \brief incremented for every cycle a ref is already locked by a client to keep track of locked duration*/
    vx_uint8               locked_count;
} reference_attr_t;

/*! \brief The Base producer structure used for both IPPC and socket
 * \ingroup group_vx_producer
 */
typedef struct
{
    /*! \brief reference object */
    tivx_reference_t       ref_base;

    /*! \brief name of the producer server */
    vx_char                name[VX_MAX_PRODUCER_NAME];
    /*! \brief name of the access point b/w producer and consumer */
    vx_char                access_point_name[VX_MAX_ACCESS_POINT_NAME];

    /*! \brief number of producer buffers */
    vx_uint32              num_buffers;
    /*! \brief number of references to be exported to consumer */
    vx_uint32              num_buffer_refs_export;   
    /*! \brief maximum number of references allowed to be locked by client before new frame is dropped instead of being sent */
    vx_uint32              max_refs_locked_by_client;
    /*! \brief Stores the buffer reference status of the producer */
    reference_attr_t    refs[VX_GC_MAX_NUM_REFS];     

    /*! \brief pointer to the producer graph object */
    void*                  graph_obj;
    /*! \brief function callbacks */
    vx_producer_dequeue_f       dequeue_callback;
    vx_producer_enqueue_f       enqueue_callback;
    vx_producer_transmit_meta_f transmit_meta;

    /*! \brief Indicates the producer state */
    producer_state_e state;

    /*! \brief Contains number of consumers connected */
    vx_uint32              nb_consumers;

    /*! \brief Thread to send broadcast information to all consumers */
    pthread_t              broadcast_thread;

    /*! \brief Contains the number of frames that has been enqueued */
    vx_uint32              nbEnqueueFrames;
    /*! \brief Contains the number of frames that has been dequeued */
    vx_uint32              nbDequeueFrames;
    /*! \brief Contains the number of frames that has been dropped */
    vx_uint32              nbDroppedFrames;

    /*! \brief Mutex to prevent conflict during setting of buffer status of multiple consumers */
    pthread_mutex_t        buffer_mutex;
    /*! \brief Flag to indicates that the reference has been exported */
    vx_bool                ref_export_done;

    /*! \brief flag to indicate if this is the last reference to be exchanged with the consumer */
    vx_uint32              last_buffer;
    /*! \brief flag to inform consumer whether previous frame has been dropped by producer */
    vx_uint8               last_frame_dropped;

    /*! \brief Mutex to prevent conflict during setting of multiple client status */
    pthread_mutex_t        client_mutex;  
} producer_base_struct_t;

/**
 * \brief Modifies the status of the specific buffer
 *
 * \param [in] buffer_id    Id of the buffer
 * \param [in] curr_status  current status of the buffer
 * \param [in] producer     Producer object
 * 
 * \retval VX_SUCCESS No errors.
 *
 * \ingroup group_vx_producer
 */
vx_status setBufferStatus(vx_int32 buffer_id, producer_buffer_stat_e curr_status, vx_producer producer);

/**
 * \brief Fetches the count of the buffer with specified status
 *
 * \param [in] producer     Producer object
 * \param [in] status       buffer status to be set
 * 
 * \retval Number of buffers
 *
 * \ingroup group_vx_producer
 */
vx_uint8 getNumBufferWithStatus(vx_producer producer, producer_buffer_stat_e status);

/**
 * \brief Fetches the id of the buffer with the given reference
 *
 * \param [in] current_ref  Input reference
 * \param [in] producer     Producer object
 * 
 * \retval Id of the buffer
 *
 * \ingroup group_vx_producer
 */
vx_int32 getBufferIdForProducer(vx_reference current_ref, vx_producer producer);

/**
 * \brief Fetches the count of locked frames for a specified client
 *
 * \param [in] producer     Producer object
 * \param [in] client       Client index
 * 
 * \retval Number of the locked frames
 *
 * \ingroup group_vx_producer
 */
vx_uint32 getNumLockedFramesByClient(vx_producer producer, vx_uint32 client);

/**
 * \brief Monitors and updates the locked buffer count in every cycle
 *
 * \param [in] producer     Producer object
 *
 * \ingroup group_vx_producer
 */
void updateLockedState(vx_producer producer);

#endif // VX_PRODUCER_H_

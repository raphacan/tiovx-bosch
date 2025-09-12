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

#ifndef VX_CONSUMER_H_
#define VX_CONSUMER_H_

#include <vx_reference.h>
#include <pthread.h>
#include <tivx_utils_ipc_ref_xfer.h>
#include <RB/vx_gc_config.h>

/*! \brief The Consumer state enumeration 
 * \ingroup group_vx_consumer
 */
typedef enum
{
    VX_CONS_STATE_DISCONNECTED = 0x0,
    VX_CONS_STATE_INIT         = 0x1,
    VX_CONS_STATE_RUN          = 0x2,
    VX_CONS_STATE_WAIT         = 0x3,
    VX_CONS_STATE_FLUSH        = 0x4,
    VX_CONS_STATE_FAILED       = 0x5
} consumer_state_e;

/*! \brief The Base producer structure used for both IPPC and socket
 * \ingroup group_vx_consumer
 */
typedef struct
{
    /*! \brief reference object */
    tivx_reference_t        ref_base;
    /*! \brief reference to implementation context */
    vx_context              context;

    /*! \brief name of the consumer client */
    vx_char                 name[VX_MAX_CONSUMER_NAME];
    /*! \brief name of the access point b/w producer and consumer */
    vx_char                 access_point_name[VX_MAX_ACCESS_POINT_NAME];

   /*! \brief The Create function */
    vx_consumer_create_graph_f create_graph_callback;
    /*! \brief The Dequeue function */
    vx_consumer_dequeue_f      dequeue_callback;
    /*! \brief The Enqueue function */
    vx_consumer_enqueue_f      enqueue_callback;
    /*! \brief The Function pointer to import metadata */
    vx_receive_meta_callback_f receive_metadata_callback;
    /*! \brief The recovery function */
    vxConsumerRecoveryCallback  recovery_callback;    

    /*! \brief Contains number of failures in producer-consumer communication */
    vx_uint32               num_failures;
    /*! \brief Flag to indicate that the consumer client initialization is done */
    vx_bool                 init_done;
    /*! \brief Flag to indicate that the consumer reference import is done */
    vx_bool                 ref_import_done;

    /*! \brief pointer to the consumer graph object */
    void*                   graph_obj;
    /*! \brief number of references imported from producer */
    vx_uint32               num_refs;
    /*! \brief Consumer references */
    vx_reference            refs[VX_GC_MAX_NUM_REFS];
    /*! \brief Mutex to prevent conflict during sending back of buffer */
    pthread_mutex_t         buffer_mutex;

    /*! \brief Contains the id of the consumer for which the data will be exchanged */
    vx_uint8                consumer_id;
    /*! \brief Thread to receive broadcasted information from producer */
    pthread_t               receiver_thread;
    /*! \brief Indicates the consumer state */
    consumer_state_e  state;
    /*! \brief Thread to send backchannel information to producer */
    pthread_t               backchannel_thread;

    /*! \brief flag to indicate that the last reference has been processed */
    vx_uint32               last_buffer;
    /*! \brief Contains the last reference buffer id */
    vx_uint8                last_buffer_id;
    /*! \brief flag to indicate that the last buffer has been transmitted */
    vx_uint8                last_buffer_transmitted;
    /*! \brief flag to inform consumer whether previous frame has been dropped by producer */
    vx_uint8                last_buffer_dropped;

    /*! \brief Array used to store intermediate IPC messages */
    tivx_utils_ref_ipc_msg_t ipcMessageArray[VX_GC_MAX_NUM_REFS];
    /*! \brief Indicates the number of IPC message sent */
    vx_uint32                ipcMessageCount;
    /*! \brief waiting time for producer */
    vx_uint32               connect_polling_time;  
} consumer_base_struct_t;

/**
 * \brief Fetches the id of the buffer with the given reference
 *
 * \param [in] buffer_ref            Input reference
 * \param [in] reference_array       Input reference array
 * \param [in] reference_array_size  reference array size
 * 
 * \retval Id of the buffer
 *
 * \ingroup group_vx_consumer
 */
vx_int32 getBufferIdForConsumer(vx_reference buffer_ref, vx_reference* reference_array, uint32_t reference_array_size);

#endif // VX_CONSUMER_H_

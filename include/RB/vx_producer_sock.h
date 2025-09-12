
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

#ifndef VX_PRODUCER_SOCK_H_
#define VX_PRODUCER_SOCK_H_

#include <utils/socket/include/app_socket.h>
#include "vx_gc_config.h"
#include "vx_producer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Producer Socket specific params
 * \ingroup group_vx_producer
 */
typedef struct _vx_gc_prod_params_t
{
    // Empty
} vx_gc_producer_params_t;

/*! \brief The runtime message b/w producer and consumer via SOCKET communication
 * \ingroup group_vx_producer
 */
typedef struct
{
    /*! \brief The buffer information exchanged b/w producer and consumer */
    buffer_info_t buffer_info;
    /*! \brief Indicates the type of message */
    message_type_e msg_type;
    /*! \brief number representing the element index for object array; set to zero if reference is not an object array item */
    vx_uint8 item_index;
    /*! \brief IPC message containing references to be exported to consumer */
    tivx_utils_ref_ipc_msg_t ref_export_handle;
    /*! \brief consumer id, used to distinguish consumers on app level */
    vx_uint8 consumer_id;
} producer_msg_content_t;

/*! \brief Backchannel information from consumer
 * \ingroup group_vx_producer
 */
typedef struct 
{
    /*! \brief Indicates the producer-consumer connection status */
    producer_client_state_e state;

    /*! \brief consumer id, used to distinguish consumers on app level */
    vx_uint8                consumer_id;

    /*! \brief Thread to receive backchannel information from consumer */
    pthread_t               bck_thread;

    /*! \brief Socket file descriptor */
    vx_int32                socket_fd;

    /*! \brief Indicates that the first buffer is released */
    vx_int32                 first_buffer_released;
} producer_bckchannel_t;

/*! \brief Producer object internal state
 * \ingroup group_vx_producer
 */
typedef struct _vx_producer
{
    producer_base_struct_t prod_base;
    /*! \brief Contains producer metadata */
    vx_uint8 metadata_buffer[VX_GC_MAX_META_SIZE];
    /*! \brief Stores consumers backchannel information */
    producer_bckchannel_t  consumers_list[VX_GC_NUM_CLIENTS];
    /*! \brief Contains server context */
    server_context         server;
} vx_producer_t;

vx_status ownInitProducerObjectSock(vx_producer producer, const vx_producer_params_t* params);

vx_status producerStartSock(vx_producer producer);

vx_status releaseProducerSock(vx_producer* producer);

#ifdef __cplusplus
}
#endif

#endif /*VX_PRODUCER_SOCK_H_*/

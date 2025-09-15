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

#ifndef VX_CONSUMER_IPPC_H_
#define VX_CONSUMER_IPPC_H_

#include <utils/ippc/include/app_ippc.h>
#include "vx_consumer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Consumer object internal state
 * \ingroup group_vx_consumer
 */
typedef struct _vx_gc_cons_params_t
{      
    /*! \brief Contains ippc port configuration */
    SIppcPortMap        ippc_port[IPPC_PORT_COUNT];
} vx_gc_consumer_params_t;

/*! \brief The consumer message content via IPPC
 * \ingroup group_vx_consumer
 */
typedef struct
{
    /*! \brief Indicates the type of message */
    message_type_e msg_type;
    /*! \brief Indicate the id of the buffer to be exchanged with the producer */
    vx_uint32 buffer_id;
    /*! \brief flag to indicate that the last reference has been processed */
    vx_uint32 last_buffer;
    /*! \brief Contains the id of the consumer for which the data has been exchanged */
    vx_uint8  consumer_id;
} consumer_msg_content_t;

/*! \brief Consumer object internal state
 * \ingroup group_vx_consumer
 */
typedef struct _vx_consumer
{      
    consumer_base_struct_t cons_base;
    /*! \brief Contains registry information */
    SIppcRegistry           m_registry;
    /*! \brief Contains receiver context */
    SIppcReceiverContext    m_receiver_ctx;
    /*! \brief Contains sender context */
    SIppcSenderContext      m_sender_ctx;
    /*! \brief Contains ippc port configuration */
    SIppcPortMap            ippc_port[IPPC_PORT_COUNT];
} vx_consumer_t;

/**
 * \brief Function to initialize consumer object for IPPC
 *
 * \param [in] producer     Consumer object
 * \param [in] params       Consumer parameter object
 * 
 * \retval VX_SUCCESS No errors.
 *
 * \ingroup group_vx_consumer
 */
vx_status ownInitConsumerObjectIppc(vx_consumer consumer, const vx_consumer_params_t* params);

VX_API_ENTRY vx_status VX_API_CALL consumerStartIppc(vx_consumer consumer);

#ifdef __cplusplus
}
#endif

#endif // VX_CONSUMER_IPPC_H_

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

#ifndef VX_CONSUMER_SOCK_H_
#define VX_CONSUMER_SOCK_H_

#include "vx_consumer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Consumer Socket specific params
 * \ingroup group_vx_consumer
 */
typedef struct _vx_gc_cons_params_t
{
    // Empty
} vx_gc_consumer_params_t;

/*! \brief Consumer object internal state
 * \ingroup group_vx_consumer
 */
typedef struct _vx_consumer
{      
    consumer_base_struct_t cons_base;
    /*! \brief Socket file descriptor */
    vx_int32               socket_fd;
} vx_consumer_t;

VX_API_ENTRY vx_status VX_API_CALL consumerStartSock(vx_consumer consumer);

#ifdef __cplusplus
}
#endif

#endif // VX_CONSUMER_SOCK_H_

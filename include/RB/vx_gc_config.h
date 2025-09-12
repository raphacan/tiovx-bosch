/*
 * Copyright (c) 2025 The Khronos Group Inc.
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

#ifndef VX_GC_CONFIG_H_
#define VX_GC_CONFIG_H_

/*! \brief The message type exchanged b/w producer and consumer 
 * \ingroup group_vx_producer
 */
typedef enum
{
    VX_MSGTYPE_HELLO                = 1U,
    VX_MSGTYPE_REF_BUF              = 2U,
    VX_MSGTYPE_BUFID_CMD            = 3U,
    VX_MSGTYPE_BUF_RELEASE          = 4U,
    VX_MSGTYPE_CONSUMER_CREATE_DONE = 5U,
    VX_MSGTYPE_COUNT                = 6U
} message_type_e;

/*! \brief max size of meta data (supplementary data)*/
#define VX_GC_MAX_META_SIZE (4096u)

/*! \brief max number of consumer for socket or IPPC*/
#define VX_GC_NUM_CLIENTS (4U)

/*! \brief max number of references*/
#define VX_GC_MAX_NUM_REFS (10u)

/*! \brief max number of items in the object array*/
#define VX_GC_MAX_NUM_ITEMS (10u)

/*! \brief max number of cycles a client is allowed to 
lock a buffer until it is returned by the connector*/
#define VX_GC_MAX_LOCKED_CNT (10U)

/*! \brief max number of times the client is allowed to recover*/
#define CONSUMER_MAX_CONSECUTIVE_FAILURES (5U)

#endif // VX_GC_CONFIG_H_

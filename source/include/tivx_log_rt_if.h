/*
*
* Copyright (c) 2018 Texas Instruments Incorporated
*
* All rights reserved not granted herein.
*
* Limited License.
*
* Texas Instruments Incorporated grants a world-wide, royalty-free, non-exclusive
* license under copyrights and patents it now or hereafter owns or controls to make,
* have made, use, import, offer to sell and sell ("Utilize") this software subject to the
* terms herein.  With respect to the foregoing patent license, such license is granted
* solely to the extent that any such patent is necessary to Utilize the software alone.
* The patent license shall not apply to any combinations which include this software,
* other than combinations with devices manufactured by or for TI ("TI Devices").
* No hardware patent is licensed hereunder.
*
* Redistributions must preserve existing copyright notices and reproduce this license
* (including the above copyright notice and the disclaimer and (if applicable) source
* code license limitations below) in the documentation and/or other materials provided
* with the distribution
*
* Redistribution and use in binary form, without modification, are permitted provided
* that the following conditions are met:
*
* *       No reverse engineering, decompilation, or disassembly of this software is
* permitted with respect to any software provided in binary form.
*
* *       any redistribution and use are licensed by TI for use only with TI Devices.
*
* *       Nothing shall obligate TI to provide you with source code for the software
* licensed and provided to you in object code.
*
* If software source code is provided to you, modification and redistribution of the
* source code are permitted provided that the following conditions are met:
*
* *       any redistribution and use of the source code, including any resulting derivative
* works, are licensed by TI for use only with TI Devices.
*
* *       any redistribution and use of any object code compiled from the source code
* and any resulting derivative works, are licensed by TI for use only with TI Devices.
*
* Neither the name of Texas Instruments Incorporated nor the names of its suppliers
*
* may be used to endorse or promote products derived from this software without
* specific prior written permission.
*
* DISCLAIMER.
*
* THIS SOFTWARE IS PROVIDED BY TI AND TI'S LICENSORS "AS IS" AND ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL TI AND TI'S LICENSORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
* OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
* OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/



#ifndef TIVX_LOG_RT_IF_H_
#define TIVX_LOG_RT_IF_H_

#include <stdint.h>

/** \brief Max name of a event identifier, MUST be 8 byte aligned */
#define TIVX_LOG_RT_EVENT_NAME_MAX      (64u)

/** \brief Max number of event identifiers, MUST be 8 byte aligned */
#define TIVX_LOG_RT_INDEX_MAX           (128u)

/** \brief Event Class: Node */
#define TIVX_LOG_RT_EVENT_CLASS_NODE        (0x0u)
/** \brief Event Class: Graph */
#define TIVX_LOG_RT_EVENT_CLASS_GRAPH       (0x1u)
/** \brief Event Class: Target */
#define TIVX_LOG_RT_EVENT_CLASS_TARGET      (0x2u)
/** \brief Event Class: Kernel instance */
#define TIVX_LOG_RT_EVENT_CLASS_KERNEL_INSTANCE (0x3u)

/** \brief Event Class: Invalid */
#define TIVX_LOG_RT_EVENT_CLASS_INVALID     (0xFFFFu)

/** \brief Event Type: Start */
#define TIVX_LOG_RT_EVENT_TYPE_START        (0u)
/** \brief Event Type: End */
#define TIVX_LOG_RT_EVENT_TYPE_END          (1u)

/** \brief Event identifier to event name index */
typedef struct {

    uint64_t event_id;    /**< event identifier */
    uint16_t event_class; /**< event class */
    uint16_t rsv[3];      /**< used to aligned to 64b */
    uint8_t  event_name[TIVX_LOG_RT_EVENT_NAME_MAX]; /**< event identifier name */

} tivx_log_rt_index_t;

/** \brief Event logger event entry */
typedef struct {

    uint64_t timestamp;   /**< timestamp of the event */
    uint64_t event_id;    /**< event identifier */
    uint32_t event_value; /**< event value */
    uint16_t event_class; /**< event class */
    uint16_t event_type;  /**< event type */

} tivx_log_rt_entry_t;


#endif /* TIVX_LOG_RT_IF_H_ */

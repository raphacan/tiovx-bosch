/*
 *
 * Copyright (c) 2017 Texas Instruments Incorporated
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

#include "TI/tivx.h"
#include "TI/tivx_capture.h"
#include "VX/vx.h"
#include "tivx_capture_kernels.h"
#include "tivx_kernel_scalar_source.h"
#include "TI/tivx_target_kernel.h"
#include <TI/tivx_task.h>
#include "tivx_kernels_target_utils.h"

typedef struct
{
    uint8_t local_val;
} tivxScalarSourceParams;

static tivx_target_kernel vx_scalar_source_target_kernel = NULL;

static vx_status VX_CALLBACK tivxScalarSourceProcess(
       tivx_target_kernel_instance kernel,
       tivx_obj_desc_t *obj_desc[],
       uint16_t num_params, void *priv_arg);
static vx_status VX_CALLBACK tivxScalarSourceCreate(
       tivx_target_kernel_instance kernel,
       tivx_obj_desc_t *obj_desc[],
       uint16_t num_params, void *priv_arg);
static vx_status VX_CALLBACK tivxScalarSourceDelete(
       tivx_target_kernel_instance kernel,
       tivx_obj_desc_t *obj_desc[],
       uint16_t num_params, void *priv_arg);

static vx_status VX_CALLBACK tivxScalarSourceProcess(
       tivx_target_kernel_instance kernel,
       tivx_obj_desc_t *obj_desc[],
       uint16_t num_params, void *priv_arg)
{
    vx_status status = VX_SUCCESS;
    tivx_obj_desc_scalar_t *out_desc;
    tivxScalarSourceParams *prms = NULL;
    uint32_t size;
    vx_enum state;

    if ( (num_params != TIVX_KERNEL_SCALAR_SOURCE_MAX_PARAMS)
        || (NULL == obj_desc[TIVX_KERNEL_SCALAR_SOURCE_OUT_IDX])
    )
    {
        status = VX_FAILURE;
    }

    if(VX_SUCCESS == status)
    {
        out_desc = (tivx_obj_desc_scalar_t *)obj_desc[TIVX_KERNEL_SCALAR_SOURCE_OUT_IDX];

    }

    if(VX_SUCCESS == status)
    {
        vx_uint8 out_value;

        status = tivxGetTargetKernelInstanceContext(kernel,
            (void **)&prms, &size);

        if (VX_SUCCESS == status)
        {
            status = tivxGetTargetKernelInstanceState(kernel, &state);

            if (VX_SUCCESS == status)
            {
                if (VX_NODE_STATE_STEADY == state)
                {
                    if (255 == prms->local_val)
                    {
                        prms->local_val = 0;
                    }
                    else
                    {
                        prms->local_val++;
                    }

                    tivxTaskWaitMsecs(1);

                    out_value = prms->local_val;

                    out_desc->data.u08 = out_value;
                }
            }
        }

    }

    return status;
}

static vx_status VX_CALLBACK tivxScalarSourceCreate(
       tivx_target_kernel_instance kernel,
       tivx_obj_desc_t *obj_desc[],
       uint16_t num_params, void *priv_arg)
{
    vx_status status = VX_SUCCESS;
    tivxScalarSourceParams *prms = NULL;

    prms = tivxMemAlloc(sizeof(tivxScalarSourceParams), TIVX_MEM_EXTERNAL);

    tivxSetTargetKernelInstanceContext(kernel, prms,
       sizeof(tivxScalarSourceParams));

    prms->local_val = 0;

    return status;
}

static vx_status VX_CALLBACK tivxScalarSourceControl(
       tivx_target_kernel_instance kernel, uint32_t node_cmd_id,
       tivx_obj_desc_t *obj_desc[],
       uint16_t num_params, void *priv_arg)
{
    vx_status status = VX_SUCCESS;
    tivx_obj_desc_scalar_t *out_desc;

    if(VX_SUCCESS == status)
    {
        out_desc = (tivx_obj_desc_scalar_t *)obj_desc[0U];
    }

    switch(node_cmd_id)
    {
        case TIVX_SCALAR_SRC_NODE_INC_SCALAR:
            out_desc->data.u08 ++;
            break;
        case TIVX_SCALAR_SRC_NODE_DEC_SCALAR:
            out_desc->data.u08 --;
            break;
    }

    return status;
}


static vx_status VX_CALLBACK tivxScalarSourceDelete(
       tivx_target_kernel_instance kernel,
       tivx_obj_desc_t *obj_desc[],
       uint16_t num_params, void *priv_arg)
{
    vx_status status = VX_SUCCESS;
    tivxScalarSourceParams *prms = NULL;
    uint32_t size;

    status = tivxGetTargetKernelInstanceContext(kernel,
        (void **)&prms, &size);

    tivxMemFree(prms, sizeof(tivxScalarSourceParams), TIVX_MEM_EXTERNAL);

    return status;
}

void tivxAddTargetKernelScalarSource(void)
{
    char target_name[TIVX_TARGET_MAX_NAME];

    if( ((vx_status)VX_SUCCESS == tivxKernelsTargetUtilsAssignTargetNameMcu(target_name)) ||
        ((vx_status)VX_SUCCESS == tivxKernelsTargetUtilsAssignTargetNameDsp(target_name)) ||
        ((vx_status)VX_SUCCESS == tivxKernelsTargetUtilsAssignTargetNameMpu(target_name)) ||
        ((vx_status)VX_SUCCESS == tivxKernelsTargetUtilsAssignTargetNameC7x(target_name)))
    {
        vx_scalar_source_target_kernel = tivxAddTargetKernelByName(
                            TIVX_KERNEL_SCALAR_SOURCE_NAME,
                            target_name,
                            tivxScalarSourceProcess,
                            tivxScalarSourceCreate,
                            tivxScalarSourceDelete,
                            tivxScalarSourceControl,
                            NULL);
    }
}

void tivxRemoveTargetKernelScalarSource(void)
{
    vx_status status = VX_SUCCESS;
    char target_name[TIVX_TARGET_MAX_NAME];

    if( ((vx_status)VX_SUCCESS == tivxKernelsTargetUtilsAssignTargetNameMcu(target_name)) ||
        ((vx_status)VX_SUCCESS == tivxKernelsTargetUtilsAssignTargetNameDsp(target_name)) ||
        ((vx_status)VX_SUCCESS == tivxKernelsTargetUtilsAssignTargetNameMpu(target_name)) ||
        ((vx_status)VX_SUCCESS == tivxKernelsTargetUtilsAssignTargetNameC7x(target_name)))
    {
        status = tivxRemoveTargetKernelByName(vx_scalar_source_target_kernel,
            TIVX_KERNEL_SCALAR_SOURCE_NAME,
            target_name);
        if (status == VX_SUCCESS)
        {
            vx_scalar_source_target_kernel = NULL;
        }
    }
}



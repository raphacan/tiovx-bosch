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

#ifndef TIVX_KERNELS_TARGET_UTILS_
#define TIVX_KERNELS_TARGET_UTILS_

#include <ti/vxlib/src/common/VXLIB_bufParams.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*tivxTargetKernel_Fxn) ();

typedef struct  {
    tivxTargetKernel_Fxn    add_kernel;
    tivxTargetKernel_Fxn    remove_kernel;
} Tivx_Target_Kernel_List;

/*!
 * \brief Registers the kernels list on the target side
 */
void tivxRegisterTargetKernels(Tivx_Target_Kernel_List *kernel_list, uint32_t num_kernels);

/*!
 * \brief Unregisters the kernels list on the target side
 */
void tivxUnRegisterTargetKernels(Tivx_Target_Kernel_List *kernel_list, uint32_t num_kernels);

/*!
 * \brief Computes the patch offset into the image
 */
static inline vx_uint32 tivxComputePatchOffset(
    vx_uint32 x, vx_uint32 y, const vx_imagepatch_addressing_t *addr);

/*!
 * \brief Check number of parameters and NULL pointers
 *
 *          First checks that the num_params is equal to the max_params
 *          defined by the OpenVX kernel.
 *
 *          Also checks that each of the obj_desc pointers are not NULL.
 *
 *          This function can be called if ALL of the parameters are
 *          mandatory.  If there are any optional parameters, then
 *          custom code should be used to check the parameters.
 */
static inline vx_status tivxCheckNullParams(
    tivx_obj_desc_t *obj_desc[], uint16_t num_params,
    uint16_t max_params);

/*!
 * \brief A utility API to initialize VXLIB buf parameters based
 *        on the provided valid rectangle and given object descriptor.
 *
 *        This API takes valid rectangle and object descriptor as an argument
 *        uses them to initialize VXLIB buf descriptor. It uses valid
 *        rectangle to initialize dimensions of the frame and object
 *        descriptor to initialize stride and data type. The buf_params array
 *        must contain the same number of elements as planes of the obj_desc.
 *        While initializing frame dimensions, it also takes into account
 *        the padding requirement of the calling kernel. If the kernel
 *        requires few pixels/lines on all sides of the kernels, this api
 *        increases the valid rectangle and then initializes vxlib buf
 *        descriptor.
 *
 *        If the valid rectangle is not provided, this API uses valid
 *        rectangle from the object descriptor.
 *
 * \param prms [in] Valid Rectangle Parameters
 */
void tivxInitBufParams(
    tivx_obj_desc_image_t *obj_desc,
    VXLIB_bufParams2D_t buf_params[]);

/*!
 * \brief A utility API to initialize two VXLIB bufparams for a kernel where
 *        width and height should be equal. The API sets both buf_params to
 *        the minimum of the valid rectangle
 */
void tivxInitTwoBufParams(
    tivx_obj_desc_image_t *obj_desc0,
    tivx_obj_desc_image_t *obj_desc1,
    VXLIB_bufParams2D_t buf_params0[],
    VXLIB_bufParams2D_t buf_params1[]);

/*!
 * \brief A utility API that sets the pointer to the correct location based on
 *        the minimum of the valid rectangle.
 */
void tivxSetPointerLocation(
    tivx_obj_desc_image_t *obj_desc,
    uint8_t *addr[]);

/*!
 * \brief A utility API that sets the pointer to the correct location based on
 *        the minimum of the valid rectangle.
 */
void tivxSetTwoPointerLocation(
    tivx_obj_desc_image_t *obj_desc0,
    tivx_obj_desc_image_t *obj_desc1,
    uint8_t *addr0[],
    uint8_t *addr1[]);

/*!
 * \brief Reserve L2MEM within C66x for usage with BAM framework
 *
 */
void tivxReserveC66xL2MEM(void);




static inline vx_uint32 tivxComputePatchOffset(
    vx_uint32 x, vx_uint32 y, const vx_imagepatch_addressing_t *addr)
{
    return (addr->stride_y * (y / addr->step_y)) +
           (addr->stride_x * (x / addr->step_x));
}

static inline vx_status tivxCheckNullParams(
    tivx_obj_desc_t *obj_desc[], uint16_t num_params,
    uint16_t max_params)
{
    vx_status status = VX_SUCCESS;
    uint32_t i;

    if (num_params != max_params)
    {
        status = VX_FAILURE;
    }
    else
    {
        for (i = 0U; i < max_params; i ++)
        {
            if (NULL == obj_desc[i])
            {
                status = VX_FAILURE;
                break;
            }
        }
    }
    return status;
}

#ifdef __cplusplus
}
#endif

#endif
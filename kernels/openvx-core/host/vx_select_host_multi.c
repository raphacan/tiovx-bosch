/*************************************************************************
 * Copyright (c) 2022 Robert Bosch GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*************************************************************************/

/*
    Implementation file for the SELECT_MULTI kernel
*/

#include <VX/vx.h>
// #include <RB/vx_rb_kernels.h>
// #include <RB/vx_rb_nodes.h>
#include <TI/tivx.h>
#include "tivx_core_host_priv.h"
#include <TI/tivx_obj_desc.h>
#include <vx_internal.h>
#include <vx_select_multi.h>

/* lookup for condition input value -> branch index*/
static const vx_uint8 branch_map[] = { 
    0, /* Invalid for condition 0 */
    VX_KERNEL_SELECT_MULTI_BRANCH_ONE,
    VX_KERNEL_SELECT_MULTI_BRANCH_TWO,
    VX_KERNEL_SELECT_MULTI_BRANCH_THREE,
    VX_KERNEL_SELECT_MULTI_BRANCH_FOUR
};

static vx_kernel vx_select_kernel_multi = NULL;

static vx_status VX_CALLBACK vxAddKernelSelectMultiValidate(vx_node node,
            const vx_reference parameters[],
            vx_uint32 num,
            vx_meta_format metas[]);

static vx_status VX_CALLBACK vxAddKernelSelectMultiInitialize(vx_node node,
            const vx_reference parameters[],
            vx_uint32 num_params);

static vx_status VX_CALLBACK vxKernelSelectMultiProcess(vx_node node,
            const vx_reference parameters[],
            vx_uint32 num);

static inline vx_status call_kernel_func(vx_enum kernel_enum, vx_bool validate_only, const vx_reference params[])
{
    vx_status status = VX_SUCCESS;
    if (ownIsValidReference(params[1]))
    {
        if (NULL != params[0]->kernel_callback)
        {
            status = (params[0]->kernel_callback)(kernel_enum, validate_only, params[0], params[1]);
        }
        else
        {
            VX_PRINT(VX_ZONE_ERROR, "Not supported\n");
            status = VX_ERROR_NOT_SUPPORTED;
        }
    }
    else
    {
        VX_PRINT(VX_ZONE_ERROR, "Invalid reference\n");
        status = VX_ERROR_INVALID_REFERENCE;
    }
    return status;
}

static vx_status VX_CALLBACK vxAddKernelSelectMultiValidate(vx_node node,
            const vx_reference parameters[],
            vx_uint32 num,
            vx_meta_format metas[])
{
    vx_status status = (vx_status)VX_SUCCESS;

    // retrieve amount of inputs used from the scalar input
    vx_uint8 num_inputs = 0;
    if (NULL != parameters[VX_KERNEL_SELECT_MULTI_NUM_INPUTS])
    {
        vxCopyScalar((vx_scalar)parameters[VX_KERNEL_SELECT_MULTI_NUM_INPUTS], &num_inputs, VX_READ_ONLY, VX_MEMORY_TYPE_HOST);
    }

    if ( (num != VX_KERNEL_SELECT_MULTI_NUM_PARAMS)
        || (NULL == parameters[VX_KERNEL_SELECT_MULTI_CONDITION_INPUT])
        || (NULL == parameters[VX_KERNEL_SELECT_MULTI_INPUT_ONE])  // mandatory
        || (NULL == parameters[VX_KERNEL_SELECT_MULTI_INPUT_TWO]) // mandatory
        || ((NULL == parameters[VX_KERNEL_SELECT_MULTI_INPUT_THREE]) && num_inputs > 2) // optional
        || ((NULL == parameters[VX_KERNEL_SELECT_MULTI_INPUT_FOUR]) && num_inputs > 3) // optional
        || (NULL == parameters[VX_KERNEL_SELECT_MULTI_OUTPUT])
    )
    {
        status = (vx_status)VX_ERROR_INVALID_PARAMETERS;
        VX_PRINT(VX_ZONE_ERROR, "One or more REQUIRED parameters are set to NULL\n");
    }

    if ((vx_status)VX_SUCCESS == status)
    {
        if ((ownIsValidReference(parameters[VX_KERNEL_SELECT_MULTI_CONDITION_INPUT])) &&
            (VX_TYPE_SCALAR == parameters[VX_KERNEL_SELECT_MULTI_CONDITION_INPUT]->type) &&
            (VX_TYPE_UINT8 == ((tivx_obj_desc_scalar_t *)parameters[VX_KERNEL_SELECT_MULTI_CONDITION_INPUT]->obj_desc)->data_type))
        {
            /* We will use VX_KERNEL_COPY to actually copy the data from one input to the output, so use the checks for that */
            vx_reference params_option_one[] = {parameters[VX_KERNEL_SELECT_MULTI_INPUT_ONE], parameters[VX_KERNEL_SELECT_MULTI_OUTPUT]};
            vx_reference params_option_two[] = {parameters[VX_KERNEL_SELECT_MULTI_INPUT_TWO], parameters[VX_KERNEL_SELECT_MULTI_OUTPUT]};
            vx_reference params_option_three[] = {parameters[VX_KERNEL_SELECT_MULTI_INPUT_THREE], parameters[VX_KERNEL_SELECT_MULTI_OUTPUT]};
            vx_reference params_option_four[] = {parameters[VX_KERNEL_SELECT_MULTI_INPUT_FOUR], parameters[VX_KERNEL_SELECT_MULTI_OUTPUT]};
            status = call_kernel_func(VX_KERNEL_COPY, vx_true_e, params_option_one);
            if (VX_SUCCESS == status)
            {
                status = call_kernel_func(VX_KERNEL_COPY, vx_true_e, params_option_two);                        

                if ((VX_SUCCESS == status) && (num_inputs > 2))
                {
                    status = call_kernel_func(VX_KERNEL_COPY, vx_true_e, params_option_three);
                    if ((VX_SUCCESS == status) && (num_inputs > 3))
                    {
                        status = call_kernel_func(VX_KERNEL_COPY, vx_true_e, params_option_four);
                   
                    }
                }
                if (tivxIsReferenceVirtual(parameters[VX_KERNEL_SELECT_MULTI_OUTPUT]))
                {
                    // why is this only done for one of the "options" in the default select node? 
                    vxSetMetaFormatFromReference(metas[VX_KERNEL_SELECT_MULTI_OUTPUT], parameters[VX_KERNEL_SELECT_MULTI_INPUT_ONE]);
                }                     
            }

            else
            {
                if (VX_ERROR_NOT_SUPPORTED == status)
                {
                    VX_PRINT(VX_ZONE_ERROR, "VX_KERNEL_SELECT_MULTI not supported for requested type\n");
                }
                else
                {
                    VX_PRINT(VX_ZONE_ERROR, "VX_KERNEL_SELECT_MULTI does not support objects of differing types or attributes\n");
                }
            }
        }
        else
        {
            VX_PRINT(VX_ZONE_ERROR, "First parameter to SELECT kernel must be a scalar containing a vx_bool value\n");
            status = VX_ERROR_NOT_COMPATIBLE;
        }
    }
    return status;
}

static vx_status VX_CALLBACK vxAddKernelSelectMultiInitialize(vx_node node,
            const vx_reference parameters[],
            vx_uint32 num_params)
{
    vx_status status = (vx_status)VX_SUCCESS;
    if ( (num_params != VX_KERNEL_SELECT_MULTI_NUM_PARAMS)
        || (NULL == parameters[VX_KERNEL_SELECT_MULTI_CONDITION_INPUT]))
    {
        status = (vx_status)VX_ERROR_INVALID_PARAMETERS;
        VX_PRINT(VX_ZONE_ERROR, "One or more REQUIRED parameters are set to NULL\n");
    }
    return status;
}

static void tivxTargetNodeDescSendComplete(
    const tivx_obj_desc_node_t *node_obj_desc)
{
    uint16_t cmd_obj_desc_id;

    if (node_obj_desc->num_out_nodes == 0U)
    {
        cmd_obj_desc_id = (uint16_t)node_obj_desc->node_complete_cmd_obj_desc_id;

        if( (vx_enum)cmd_obj_desc_id != (vx_enum)TIVX_OBJ_DESC_INVALID)
        {
            tivx_obj_desc_cmd_t *cmd_obj_desc = (tivx_obj_desc_cmd_t *)ownObjDescGet(cmd_obj_desc_id);

            if( ownObjDescIsValidType( (tivx_obj_desc_t*)cmd_obj_desc, TIVX_OBJ_DESC_CMD) != 0)
            {
                uint64_t timestamp = tivxPlatformGetTimeInUsecs()*1000U;

                tivx_uint64_to_uint32(
                    timestamp,
                    &cmd_obj_desc->timestamp_h,
                    &cmd_obj_desc->timestamp_l
                );

                /* if this is leaf node: send complete command to host
                 */
                ownObjDescSend( cmd_obj_desc->dst_target_id, cmd_obj_desc_id);
            }
        }
    }
}

/* Notice we have two sets of parameters, those initially set on the node,
   and those passed to this function by the calling engine. For replicated
   nodes these will differ. We use the original parameters to retrieve the
   optimisation information, and the second set of parameters to retrieve
   the specific values to use on this call of the node.
   The calling engine also adds as an extra parameter the node descriptor,
   this enables us to find the pipeline id.
*/
static vx_status VX_CALLBACK vxKernelSelectMultiProcess(vx_node node,
            const vx_reference parameters[],
            vx_uint32 num)
{
    vx_status status = VX_SUCCESS;
    if (VX_KERNEL_SELECT_MULTI_NUM_PARAMS != num ||
        VX_SUCCESS != vxGetStatus(parameters[VX_KERNEL_SELECT_MULTI_CONDITION_INPUT]))
    {
        status = VX_FAILURE;
    }
    else
    {
        vx_reference * original_params = node->parameters;
        tivx_obj_desc_node_t *my_objd = (tivx_obj_desc_node_t *)parameters[num];
        vx_uint32 pipeline_id = my_objd->pipeline_id;
        vx_uint8 condition = ((tivx_obj_desc_scalar_t *)parameters[VX_KERNEL_SELECT_MULTI_CONDITION_INPUT]->obj_desc)->data.u08;
        VX_PRINT(VX_ZONE_INFO, "Kernel SELECT pipeline %d; condition is %d\n", pipeline_id, condition);

        // retrieve amount of inputs used from the scalar input
        vx_uint8 num_inputs = 0;
        if (NULL != parameters[VX_KERNEL_SELECT_MULTI_NUM_INPUTS])
        {
            vxCopyScalar((vx_scalar)parameters[VX_KERNEL_SELECT_MULTI_NUM_INPUTS], &num_inputs, VX_READ_ONLY, VX_MEMORY_TYPE_HOST);
        }  
        if (num_inputs < 2 || num_inputs > VX_KERNEL_SELECT_MULTI_MAX_NUM_INPUTS)
        {
            VX_PRINT(VX_ZONE_ERROR, "Invalid number of inputs %d for SELECT_MULTI kernel\n", num_inputs);
            return VX_ERROR_INVALID_PARAMETERS;
        }

        if (NULL != original_params[VX_KERNEL_SELECT_MULTI_BRANCH_TWO]) // check why in initial implementation false branch was used to detect presence of optimization
        {
            /* We are doing an optimised operation. We effectively remove nodes on one branch from the graph */
            vx_uint32 ix;
            vx_uint32 n;
            vx_uint32 i_cond;
            for (i_cond = 1U; i_cond < 5; i_cond++)
            {
                if (condition != i_cond)
                {
                    ix = branch_map[i_cond];
                    VX_PRINT(VX_ZONE_INFO, "Kernel SELECT pipeline %d; removing branch %d\n", pipeline_id, i_cond);
                    for (n = 0; n < VX_KERNEL_SELECT_MAX_NODES_IN_BRANCH && NULL != original_params[n + ix]; ++n)
                    {
                        tivx_obj_desc_node_t *node_desc = ((vx_node)original_params[n + ix])->obj_desc[pipeline_id];
                        /* mark node as complete. */
                        tivxFlagBitSet(&node_desc->flags, TIVX_NODE_FLAG_IS_EXECUTED);
                        /* Now we have to remove this node from the list of nodes blocked by the SELECT node */
                        vx_uint32 i;
                        for (i = 0; i < my_objd->num_out_nodes; ++i)
                        {
                            if (my_objd->out_node_id[i] == node_desc->base.obj_desc_id)
                            {
                                my_objd->num_out_nodes--;
                                my_objd->out_node_id[i] = my_objd->out_node_id[my_objd->num_out_nodes];
                                my_objd->out_node_id[my_objd->num_out_nodes] = 0;
                                break;
                            }
                        }
                    }
                }
            }

            /* Now we have to add the nodes of the other branch to the list of nodes blocked by the SELECT
               node, so that they may be queued for execution by the framework when this execution finishes */
            for (n = 0; n < VX_KERNEL_SELECT_MAX_NODES_IN_BRANCH && NULL != original_params[n + branch_map[condition]]; ++n)
            {
                my_objd->out_node_id[my_objd->num_out_nodes++] = ((vx_node)original_params[n + branch_map[condition]])->obj_desc[pipeline_id]->base.obj_desc_id;
            }
        }
        else
        {
            /* We are doing an unoptimised or less optimised operation, just copy or move the correct input to the output.
             * The kernel enum to use (Copy or Move) is supplied in parameter[VX_KERNEL_SELECT_TRUE_KERNEL] or [VX_KERNEL_SELECT_FALSE_KERNEL]
            */
            vx_uint8 use_input = (condition > 0 && condition <= 4) ? condition : 0; // value of condition maps to input indexes

            vx_reference params[] = {parameters[use_input], parameters[VX_KERNEL_SELECT_MULTI_OUTPUT]};
            vx_enum kernel_enum = (vx_enum)(uintptr_t)original_params[use_input];
            status = call_kernel_func(kernel_enum, vx_false_e, params);
        }
    }
    return status;
}

vx_status tivxAddKernelSelectMulti(vx_context context)
{
    vx_kernel kernel = vxAddUserKernel(
                context,
                "org.khronos.openvx.select_multi",
                VX_KERNEL_SELECT_MULTI,
                vxKernelSelectMultiProcess,
                VX_KERNEL_SELECT_MULTI_NUM_PARAMS,
                vxAddKernelSelectMultiValidate,
                vxAddKernelSelectMultiInitialize,
                NULL);
    vx_status status = vxGetStatus((vx_reference)kernel);
    if (VX_SUCCESS == status)
     {
        status = vxAddParameterToKernel(kernel,
                        VX_KERNEL_SELECT_MULTI_CONDITION_INPUT,
                        (vx_enum)VX_INPUT,
                        (vx_enum)VX_TYPE_SCALAR,
                        (vx_enum)VX_PARAMETER_STATE_REQUIRED);
    }
    if (VX_SUCCESS == status)
    {
        status = vxAddParameterToKernel(kernel,
                        VX_KERNEL_SELECT_MULTI_INPUT_ONE,
                        (vx_enum)VX_INPUT,
                        (vx_enum)VX_TYPE_REFERENCE,
                        (vx_enum)VX_PARAMETER_STATE_REQUIRED);
    }
    if (VX_SUCCESS == status)
    {
        status = vxAddParameterToKernel(kernel,
                        VX_KERNEL_SELECT_MULTI_INPUT_TWO,
                        (vx_enum)VX_INPUT,
                        (vx_enum)VX_TYPE_REFERENCE,
                        (vx_enum)VX_PARAMETER_STATE_REQUIRED);
    }
    if (VX_SUCCESS == status)
    {
        status = vxAddParameterToKernel(kernel,
                        VX_KERNEL_SELECT_MULTI_INPUT_THREE,
                        (vx_enum)VX_INPUT,
                        (vx_enum)VX_TYPE_REFERENCE,
                        (vx_enum)VX_PARAMETER_STATE_OPTIONAL);
    }
    if (VX_SUCCESS == status)
    {
        status = vxAddParameterToKernel(kernel,
                        VX_KERNEL_SELECT_MULTI_INPUT_FOUR,
                        (vx_enum)VX_INPUT,
                        (vx_enum)VX_TYPE_REFERENCE,
                        (vx_enum)VX_PARAMETER_STATE_OPTIONAL);
    }
    if (VX_SUCCESS == status)
    {
        status = vxAddParameterToKernel(kernel,
                        VX_KERNEL_SELECT_MULTI_NUM_INPUTS,
                        (vx_enum)VX_INPUT,
                        (vx_enum)VX_TYPE_REFERENCE,
                        (vx_enum)VX_PARAMETER_STATE_REQUIRED);
    }
    if (VX_SUCCESS == status)
    {
        status = vxAddParameterToKernel(kernel,
                        VX_KERNEL_SELECT_MULTI_OUTPUT,
                        (vx_enum)VX_OUTPUT,
                        (vx_enum)VX_TYPE_REFERENCE,
                        (vx_enum)VX_PARAMETER_STATE_REQUIRED);
    }
    if (VX_SUCCESS == status)
    {
        vxFinalizeKernel(kernel);
    }
    if (status != (vx_status)VX_SUCCESS)
    {
        vxReleaseKernel(&kernel);
        vx_select_kernel_multi = NULL;
    }
    else
    {
        vx_select_kernel_multi = kernel;
    }
    return status;
}

vx_status tivxRemoveKernelSelectMulti(vx_context context)
{
    vx_status status;
    status = vxRemoveKernel(vx_select_kernel_multi);
    vx_select_kernel_multi = NULL;
    return status;
}

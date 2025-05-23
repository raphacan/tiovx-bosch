/*
*
* Copyright (c) 2017-2021 Texas Instruments Incorporated
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

#include <tivx_platform_posix.h>

void init_params(tivx_task_create_params_t ** const params);

void init_params(tivx_task_create_params_t ** const params)
{
    (*params)->stack_ptr = NULL;
    (*params)->stack_size = (uint32_t)0;
    (*params)->core_affinity = (uint32_t)0;
    (*params)->priority = (uint32_t)0;
    (*params)->task_main = NULL;
    (*params)->app_var = NULL;
    (void)memset(&(*params)->task_name, 0, sizeof((*params)->task_name));

    return;
}

void tivxTaskSetDefaultCreateParams(tivx_task_create_params_t *params)
{
    if (NULL != params)
    {
        init_params(&params);

        params->core_affinity = TIVX_TASK_AFFINITY_ANY;
        params->priority = TIVX_TASK_PRI_LOWEST;
    }
}

static void *tivxTaskMain(void *arg)
{
    tivx_task *task = (tivx_task*)arg;

    if( task /* TIOVX-1952- LDRA Uncovered Branch Id: TIOVX_BRANCH_COVERAGE_TIVX_TASK_UBR001 */
    && task->task_func) /* TIOVX-1952- LDRA Uncovered Branch Id: TIOVX_BRANCH_COVERAGE_TIVX_TASK_UBR002 */
    {
        task->task_func(task->app_var);
    }

    return NULL;
}

vx_status tivxTaskCreate(tivx_task *task, const tivx_task_create_params_t *params)
{
    vx_status status = (vx_status)VX_SUCCESS;

    if ((NULL != task) && (NULL != params) && (NULL != params->task_main))
    {
        tivx_task_context context;

        task->tsk_handle = NULL;

        context = (tivx_task_context)ownPosixObjectAlloc((vx_enum)TIVX_POSIX_TYPE_TASK);
        if(context == NULL)
        {
            VX_PRINT(VX_ZONE_ERROR, "Context memory allocation failed\n");
            status = (vx_status)VX_ERROR_NO_MEMORY;
        }
        else
        {
            pthread_attr_t thread_attr;

            task->stack_ptr = params->stack_ptr;
            task->stack_size = params->stack_size;
            task->core_affinity = params->core_affinity;
            task->priority = params->priority;
            task->task_func = params->task_main;
            task->app_var = params->app_var;

            status = pthread_attr_init(&thread_attr);

/* LDRA_JUSTIFY_START
<metric start> branch <metric end>
<justification start> TIOVX_BRANCH_COVERAGE_TIVX_TASK_UBR003
<justification end> */
            if(status==0)
/* LDRA_JUSTIFY_END */
            {
                {
                    if(task->stack_size>0u)
                    {
                        status = pthread_attr_setstacksize(&thread_attr, task->stack_size);
                    }
                }

/* LDRA_JUSTIFY_START
<metric start> statement branch <metric end>
<justification start> TIOVX_BRANCH_COVERAGE_TIVX_TASK_UBR004
<justification end> */
                if(status==0)
/* LDRA_JUSTIFY_END */
                {
                    status = pthread_create(&context->hndl, &thread_attr, tivxTaskMain, task);
                }
/* LDRA_JUSTIFY_START
<metric start> statement branch <metric end>
<justification start> TIOVX_BRANCH_COVERAGE_TIVX_TASK_UBR004
<justification end> */
                else
                {
                    VX_PRINT(VX_ZONE_ERROR, "Task creation failed.  Invalid stack_size provided\n");
                }
/* LDRA_JUSTIFY_END */
                (void)pthread_attr_destroy(&thread_attr);
            }
/* LDRA_JUSTIFY_START
<metric start> statement branch <metric end>
<justification start> TIOVX_BRANCH_COVERAGE_TIVX_TASK_UBR005
<justification end> */
            if (status == 0)
/* LDRA_JUSTIFY_END */
            {
                task->tsk_handle = (void *)context;
            }
/* LDRA_JUSTIFY_START
<metric start> statement branch <metric end>
<justification start> TIOVX_CODE_COVERAGE_TASK_UM001
<justification end> */
            else
            {
                (void)ownPosixObjectFree((uint8_t*)context, (vx_enum)TIVX_POSIX_TYPE_TASK);
            }
/* LDRA_JUSTIFY_END */
        }
    }
    else
    {
        VX_PRINT(VX_ZONE_ERROR, "Task or params arguments are NULL\n");
        status = (vx_status)VX_FAILURE;
    }

    return (status);
}

vx_status tivxTaskDelete(tivx_task *task)
{
    vx_status status = (vx_status)VX_SUCCESS;

    if(task && task->tsk_handle)
    {
        tivx_task_context context;
        void *ret_val;

        context = (tivx_task_context)task->tsk_handle;

        (void)pthread_cancel(context->hndl);
        (void)pthread_join(context->hndl, &ret_val);

        status = ownPosixObjectFree((uint8_t*)context, (vx_enum)TIVX_POSIX_TYPE_TASK);
/* LDRA_JUSTIFY_START
<metric start> statement branch <metric end>
<justification start> TIOVX_CODE_COVERAGE_TASK_UM002
<justification end> */
        if ((vx_status)VX_SUCCESS != status)
        {
            VX_PRINT(VX_ZONE_ERROR, "Task free failed\n");
        }
/* LDRA_JUSTIFY_END */

        task->tsk_handle = NULL;
    }
    else
    {
        VX_PRINT(VX_ZONE_ERROR, "Task or task handle are NULL\n");
        status = (vx_status)VX_FAILURE;
    }

    return (status);
}

void tivxTaskWaitMsecs(uint32_t msec)
{
#if defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE >= 199309L)
    struct timespec delay_time = {0}, remain_time = {0};
    int ret;

    delay_time.tv_sec  = (int64_t)msec/1000;
    delay_time.tv_nsec = ((int64_t)msec%1000)*1000000;

    do
    {
        ret = nanosleep(&delay_time, &remain_time);
/* LDRA_JUSTIFY_START
<metric start> statement branch <metric end>
<justification start> TIOVX_CODE_COVERAGE_TASK_UM003
<justification end> */
        if((ret < 0) && (remain_time.tv_sec > 0) && (remain_time.tv_nsec > 0))
        {
            /* restart for remaining time */
            delay_time = remain_time;
        }
/* LDRA_JUSTIFY_END */
/* LDRA_JUSTIFY_START
<metric start> statement branch <metric end>
<justification start> TIOVX_CODE_COVERAGE_TASK_UM003
<justification end> */
        else
/* LDRA_JUSTIFY_END */
        {
            break;
        }
/* LDRA_JUSTIFY_START
<metric start> statement <metric end>
<justification start> TIOVX_CODE_COVERAGE_TASK_UM003
<justification end> */
    }
/* LDRA_JUSTIFY_END */
/* LDRA_JUSTIFY_START
<metric start> statement branch <metric end>
<justification start> TIOVX_CODE_COVERAGE_TASK_UM003
<justification end> */
    while(true);
/* LDRA_JUSTIFY_END */
#else
    (void)usleep(msec * 1000U);
#endif
}


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

#include <pthread.h>
#include <tivx_platform_posix.h>

#include <vx_internal.h>
#include <tivx_platform_pc.h>

void tivxRegisterOpenVXCoreTargetKernels(void);
void tivxUnRegisterOpenVXCoreTargetKernels(void);

void tivxRegisterTutorialTargetKernels(void);
void tivxUnRegisterTutorialTargetKernels(void);

/* Mutex for controlling access to Init/De-Init. */
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Counter for tracking the {init, de-init} calls. This is also used to
 * guarantee a single init/de-init operation.
 */
static uint32_t g_init_status = 0U;

void ownRegisterKernels()
{
    /* trick target kernel used in DSP emulation mode to think
     * they are being invoked from a DSP
     */
    tivxSetSelfCpuId((vx_enum)TIVX_CPU_ID_DSP1);
    tivxRegisterOpenVXCoreTargetKernels();
    #ifdef BUILD_TUTORIAL
    tivxRegisterTutorialTargetKernels();
    #endif

    #if defined (SOC_J721E) || defined (SOC_J722S)
        tivxSetSelfCpuId((vx_enum)TIVX_CPU_ID_DSP2);
        tivxRegisterOpenVXCoreTargetKernels();
        #ifdef BUILD_TUTORIAL
        tivxRegisterTutorialTargetKernels();
        #endif
    #endif
}

void ownUnregisterKernels()
{
    tivxSetSelfCpuId((vx_enum)TIVX_CPU_ID_DSP1);
    tivxUnRegisterOpenVXCoreTargetKernels();
    #ifdef BUILD_TUTORIAL
    tivxUnRegisterTutorialTargetKernels();
    #endif

    #if defined (SOC_J721E) || defined (SOC_J722S)
        tivxSetSelfCpuId((vx_enum)TIVX_CPU_ID_DSP2);
        tivxUnRegisterOpenVXCoreTargetKernels();
        #ifdef BUILD_TUTORIAL
        tivxUnRegisterTutorialTargetKernels();
        #endif
    #endif
}

void tivxInit(void)
{
    pthread_mutex_lock(&g_mutex);

    if (0U == g_init_status)
    {
        tivx_set_debug_zone(VX_ZONE_INFO);
        tivx_set_debug_zone(VX_ZONE_ERROR);
        tivx_set_debug_zone(VX_ZONE_WARNING);

        /* Initialize the POSIX objects */
        ownPosixObjectInit();

        /* Initialize resource logging */
        ownLogResourceInit();

        /* Initialize platform */
        ownPlatformInit();

        /* Initialize Target */
        ownTargetInit();

        /* Register Kernels */
        ownRegisterKernels();

        /* let rest of system think it is running on CPU 0 */
        tivxSetSelfCpuId((vx_enum)TIVX_CPU_ID_MPU_0);

        ownObjDescInit();

        ownPlatformCreateTargets();

        VX_PRINT(VX_ZONE_INFO, "Initialization Done !!!\n");
        tivx_clr_debug_zone(VX_ZONE_INFO);
    }

    g_init_status++;

    pthread_mutex_unlock(&g_mutex);
}

void tivxDeInit(void)
{
    pthread_mutex_lock(&g_mutex);

    if (0U != g_init_status)
    {
        g_init_status--;

        if (0U == g_init_status)
        {
            ownPlatformDeleteTargets();

            /* Unregister Kernels */
            ownUnregisterKernels();

            /* let rest of system think it is running on CPU 0 */
            tivxSetSelfCpuId((vx_enum)TIVX_CPU_ID_MPU_0);

            /* DeInitialize Target */
            ownTargetDeInit();

            /* DeInitialize platform */
            ownPlatformDeInit();

            /* DeInitialize resource logging */
            ownLogResourceDeInit();

            /* DeInitialize the POSIX objects */
            ownPosixObjectDeInit();
        }
    }
    else
    {
        /* ERROR. */
        VX_PRINT(VX_ZONE_ERROR, "De-Initialization Error !!!\n");
    }

    pthread_mutex_unlock(&g_mutex);
}

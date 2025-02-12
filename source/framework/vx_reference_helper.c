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

#include <vx_internal.h>

VX_API_ENTRY vx_reference VX_API_CALL vxReferenceGetScope(vx_reference reference, vx_status *status)
{
    vx_reference scopeRef = NULL;
    if (NULL != reference)
    {
        if(ownIsValidReference(reference) == (vx_bool)vx_true_e)
        {
            if (NULL != reference->scope)
            {
                if(ownIsValidReference(reference->scope) == (vx_bool)vx_true_e)
                {
                    scopeRef = reference->scope;
                    *status = (vx_status)VX_SUCCESS;
                }
                else
                {
                    VX_PRINT(VX_ZONE_ERROR, "Invalid scope reference\n");
                    *status = (vx_status)VX_ERROR_INVALID_SCOPE;
                }
            }
            else
            {
                VX_PRINT(VX_ZONE_ERROR, "Scope reference is NULL\n");
                *status = (vx_status)VX_ERROR_INVALID_SCOPE;
            }
        }
        else
        {
            VX_PRINT(VX_ZONE_ERROR, "Invalid reference passed\n");
            *status = (vx_status)VX_ERROR_INVALID_REFERENCE;
        }
    }
    else
    {
        VX_PRINT(VX_ZONE_ERROR, "Invalid reference passed\n");
        *status = (vx_status)VX_ERROR_INVALID_REFERENCE;
    }

    return scopeRef;
}
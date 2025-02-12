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

#ifndef _VX_KHR_REFERENCE_HELPER_H_
#define _VX_KHR_REFERENCE_HELPER_H_

/*!
* \file
* \brief The OpenVX producer extension API.
*/

#define OPENVX_KHR_REFERENCE_HELPER  "vx_khr_reference_helper"

#include <VX/vx.h>


#ifdef __cplusplus
extern "C"
{
#endif

/*! \brief Retrieves the scope of a reference
* \param [in] reference The reference from which to retrieve the scope reference.
* \param [in] status    status value to indicate whether retrieval of scope reference was successfull.
* \ingroup group_context
* \return the scope reference of the input reference. The user has the responsibility to check 
*         whether the reference returned is of the expected type
*/
VX_API_ENTRY vx_reference VX_API_CALL vxReferenceGetScope(vx_reference reference, vx_status *status);
    
#ifdef __cplusplus
}
#endif

#endif // _VX_KHR_REFERENCE_HELPER_H_
 
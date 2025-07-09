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
#ifndef _OPENVX_SELECT_MULTI_H_
#define _OPENVX_SELECT_MULTI_H_

#ifdef  __cplusplus
extern "C" {
#endif

/* Header for the Select node utility functions */

#define VX_KERNEL_SELECT_MULTI_NUM_PARAMS (7u)
#define VX_KERNEL_SELECT_MULTI_CONDITION_INPUT (0u)
#define VX_KERNEL_SELECT_MULTI_INPUT_ONE (1u)
#define VX_KERNEL_SELECT_MULTI_INPUT_TWO (2u)
#define VX_KERNEL_SELECT_MULTI_INPUT_THREE (3u)
#define VX_KERNEL_SELECT_MULTI_INPUT_FOUR (4u)
#define VX_KERNEL_SELECT_MULTI_NUM_INPUTS (5u) // number of inputs used in the select multi node
#define VX_KERNEL_SELECT_MULTI_OUTPUT (6u)
#define VX_KERNEL_SELECT_MULTI_KERNEL_ONE (7u)
#define VX_KERNEL_SELECT_MULTI_KERNEL_TWO (8u)
#define VX_KERNEL_SELECT_MULTI_KERNEL_THREE (9u)
#define VX_KERNEL_SELECT_MULTI_KERNEL_FOUR (10u)
#define VX_KERNEL_SELECT_MULTI_BRANCH_ONE (11u)
#define VX_KERNEL_SELECT_MAX_NODES_IN_BRANCH (5u)
#define VX_KERNEL_SELECT_MULTI_BRANCH_TWO (VX_KERNEL_SELECT_MULTI_BRANCH_ONE + VX_KERNEL_SELECT_MAX_NODES_IN_BRANCH)
#define VX_KERNEL_SELECT_MULTI_BRANCH_THREE (VX_KERNEL_SELECT_MULTI_BRANCH_TWO + VX_KERNEL_SELECT_MAX_NODES_IN_BRANCH)
#define VX_KERNEL_SELECT_MULTI_BRANCH_FOUR (VX_KERNEL_SELECT_MULTI_BRANCH_THREE + VX_KERNEL_SELECT_MAX_NODES_IN_BRANCH)
#define VX_KERNEL_SELECT_MULTI_MAX_PARAM_USED (VX_KERNEL_SELECT_MULTI_BRANCH_FOUR + VX_KERNEL_SELECT_MAX_NODES_IN_BRANCH)
#define VX_KERNEL_SELECT_MULTI_MAX_NUM_INPUTS (4u) // maximum number of inputs used in the select multi node

/* Check configuration consistency */
#if (VX_KERNEL_SELECT_MAX_PARAM_USED > TIVX_KERNEL_MAX_PARAMS)
#error TIVX_KERNEL_MAX_PARAMS must be set to at least the value of VX_KERNEL_SELECT_MAX_PARAMS_USED
#endif

/*! \brief Graph Verification: Process Select node, identifying the branches feeding
 * the true and false condition inputs, and change execution order to process the condition
 * input early.
 * \param [in] graph - the graph to process. In and out nodes must already have been calculated,
 *                      Copy and MOve nodes processed, and dual writers identified.
 * \returns VX_SUCCESS if all OK
 */
vx_status ownGraphProcessSelectMultiNodes(vx_graph graph);

/*! \brief Graph Verification: Process Select node, identifying trivial cases where the
 * true_value and false_value inputs are the same. These nodes could be replaced with Copy nodes.
 * This function must be called before the Copy and Move nodes are processed.
 * \param [in] graph - the graph to process. In and out nodes must already have been calculated.
 * \returns VX_SUCCESS if all OK
 */
vx_status ownGraphEliminateTrivialSelectMultiNodes(vx_graph graph);

#ifdef  __cplusplus
}
#endif

#endif

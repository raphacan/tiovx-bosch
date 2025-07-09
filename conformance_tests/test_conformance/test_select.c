/*
 * Copyright (c) 2012-2018 The Khronos Group Inc.
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

//#include "test_tiovx.h"
#include <TI/tivx.h>
#include "test_engine/test.h"
#include <VX/vx.h>
#include <VX/vxu.h>
#include <VX/vx_khr_safe_casts.h>
#include <VX/vx_khr_pipelining.h>

#include <TI/tivx_config.h>
#include "math.h"
#include <limits.h>
#include <TI/tivx_test_kernels.h>
#include <TI/tivx_capture.h>
#include <TI/tivx_task.h>

#if defined(SOC_AM62A)
#define TIVX_TARGET_MCU TIVX_TARGET_MCU1_0
#else
#define TIVX_TARGET_MCU TIVX_TARGET_MCU2_0
#endif

TESTCASE(SelectMulti,  CT_VXContext, ct_setup_vx_context, 0)

#define LOG_RT_TRACE_ENABLE       (1u)

#define MAX_NUM_BUF               (8u)
#define MAX_IMAGE_PLANES          (3u)
#define MAX_NUM_OBJ_ARR_ELEMENTS  (4u)
#define MAX_NUM_PYR_ELEMENTS  (4u)

#define GRAPH_CONSUMED_EVENT      (1u)
#define NODE0_COMPLETED_EVENT     (2u)
#define NODE1_COMPLETED_EVENT     (3u)
#define GRAPH_COMPLETED_EVENT     (4u)

typedef struct {
    const char* testName;
    int width, height;
    int pipe_depth;
    int num_buf;
    int loop_count;
    int measure_perf;
} Arg;

/* Function to get a parameter, add it to a graph and then release it */
static void addParameterToGraph(vx_graph graph, vx_node node, vx_uint32 num)
{
    vx_parameter p = vxGetParameterByIndex(node, num);
    vxAddParameterToGraph(graph, p);
    vxReleaseParameter(&p);
}

/* create a graph with pipelining: 

                                                           copy_node1 - image2
                                                         /                     \ 
        (Graph Parameter 0) image - copy_node0 - image1 /                       select_node - Selectoutput - copy_node3 - output (Graph parameter2)                    
                                                      \ \                      /   /  |
                                                       \ \                    /   /   |
                                                        \  copy_node2  - image3  /    |
                                                         \                      /     |
                                                          \                    /      |
                                                            copy_node4 - image4       |
                            (Graph Parameter1) scalar---copy_node---------------------
    */
TEST(SelectMulti, testSelectMultiNodePipeline_try_replicate_realsetup)
{
    
    vx_context context = context_->vx_context_;
    tivx_set_debug_zone(VX_ZONE_ERROR);
    tivx_set_debug_zone(VX_ZONE_WARNING);
    tivx_set_debug_zone(VX_ZONE_INFO);
    vx_graph graph = vxCreateGraph(context);
    vx_uint8  scalar_val = 2;
    VX_PRINT(VX_ZONE_INFO, "scalar_val = %d\n", scalar_val);
    vx_status status = VX_FAILURE;
    vx_image image = vxCreateImage(context, 64, 48, VX_DF_IMAGE_U8);
    vxSetReferenceName((vx_reference)image, "image");

    vx_image image1 = vxCreateImage(context, 64, 48, VX_DF_IMAGE_U8);
    vxSetReferenceName((vx_reference)image1, "image1");
    vx_image image2 = vxCreateVirtualImage(graph, 64, 48, VX_DF_IMAGE_U8);
    vxSetReferenceName((vx_reference)image2, "image2");
    vx_image image3 = vxCreateVirtualImage(graph, 64, 48, VX_DF_IMAGE_U8);
    vxSetReferenceName((vx_reference)image3, "image3");
    vx_image image4 = vxCreateVirtualImage(graph, 64, 48, VX_DF_IMAGE_U8);
    vxSetReferenceName((vx_reference)image4, "image4");
    vx_scalar scalar = vxCreateScalar(context, VX_TYPE_UINT8, &scalar_val);
    vxSetReferenceName((vx_reference)scalar, "scalar");
    vx_scalar scalar_out = vxCreateScalar(context, VX_TYPE_UINT8, &scalar_val);
    vxSetReferenceName((vx_reference)scalar_out, "scalar_out");
    vx_image Selectoutput = vxCreateImage(context, 64, 48, VX_DF_IMAGE_U8);
    vxSetReferenceName((vx_reference)Selectoutput, "Selectoutput");
    vx_image output = vxCreateImage(context, 64, 48, VX_DF_IMAGE_U8);
    vxSetReferenceName((vx_reference)output, "output");

    vx_node copy_node0 = vxCopyNode(graph, image, image1);
    vxSetReferenceName((vx_reference)copy_node0, "copy_node0");
    vx_node copy_node1 = vxCopyNode(graph, image1, image2);
    vxSetReferenceName((vx_reference)copy_node1, "copy_node1");
    vx_node copy_node2 = vxCopyNode(graph, image1, image3);
    vxSetReferenceName((vx_reference)copy_node2, "copy_node2");
    vx_node copy_node4 = vxCopyNode(graph, image1, image4);
    vxSetReferenceName((vx_reference)copy_node4, "copy_node4");
    vx_node copy_scalar_node = vxCopyNode(graph, scalar, scalar_out);
    vxSetReferenceName((vx_reference)copy_scalar_node, "copy_scalar_node");
    vx_reference refs[4] = { (vx_reference)image2, (vx_reference)image3, (vx_reference)image4, NULL};
    vx_uint8 numInputs = 3;
    vx_scalar scalar_numInputs = vxCreateScalar(context, VX_TYPE_UINT8, &numInputs);
    vxSetReferenceName((vx_reference)scalar_numInputs, "scalar_numInputs");
    vx_node select_node = vxSelectNodeMulti(graph, scalar_out, refs, scalar_numInputs, Selectoutput);
    vxSetReferenceName((vx_reference)select_node, "select_node");
    vx_node copy_node3 = vxCopyNode(graph, Selectoutput, output);
    vxSetReferenceName((vx_reference)copy_node3, "copy_node3");


    addParameterToGraph(graph, copy_node0, 0);  // input copy node  
    addParameterToGraph(graph, copy_scalar_node, 0); // input of copy node that copies the scalar value
    addParameterToGraph(graph, copy_node3, 1);  // output copy_node3

    vx_graph_parameter_queue_params_t graph_params[3] =
    {
        {.graph_parameter_index = 0, .refs_list = (vx_reference *)&image, .refs_list_size = 1}, 
        {.graph_parameter_index = 1, .refs_list = (vx_reference *)&scalar, .refs_list_size = 1},
        {.graph_parameter_index = 2, .refs_list = (vx_reference *)&output, .refs_list_size = 1}
    };

    ASSERT_EQ_VX_STATUS(VX_SUCCESS, vxSetGraphScheduleConfig(graph, VX_GRAPH_SCHEDULE_MODE_QUEUE_AUTO, 3, graph_params));

    status = vxVerifyGraph(graph);

    // enqueue all parameters to the graph
    ASSERT_EQ_VX_STATUS(status, VX_SUCCESS);
    ASSERT_EQ_VX_STATUS(VX_SUCCESS, vxGraphParameterEnqueueReadyRef(graph, 0, (vx_reference *)&image, 1));
    ASSERT_EQ_VX_STATUS(VX_SUCCESS, vxGraphParameterEnqueueReadyRef(graph, 1, (vx_reference *)&scalar, 1));
    ASSERT_EQ_VX_STATUS(VX_SUCCESS, vxGraphParameterEnqueueReadyRef(graph, 2, (vx_reference *)&output, 1));

    // process the graph
    vx_reference dequeue_ref;
    vx_uint32 num_dequeued_refs;
    vxGraphParameterDequeueDoneRef(graph, 0, &dequeue_ref, 1, &num_dequeued_refs);
    vxGraphParameterDequeueDoneRef(graph, 1, &dequeue_ref, 1, &num_dequeued_refs);
    vxGraphParameterDequeueDoneRef(graph, 2, &dequeue_ref, 1, &num_dequeued_refs);
    char filepath[MAXPATHLENGTH];
    char filename_prefix[64];
    size_t sz = 0;
    sz = snprintf(filepath, MAXPATHLENGTH, "%s/output", "/media/mfl7si/d24ee47d-1d27-4c3b-a5a7-5ec4599bcb63/openvx_out");
    tivxExportGraphToDot(graph, "/media/mfl7si/d24ee47d-1d27-4c3b-a5a7-5ec4599bcb63/openvx_out", "select_multi_node_graph");

    // change the scalar value and re enqueue/dequeue to see different path taken
    scalar_val = 1;
    vxCopyScalar(scalar, &scalar_val, VX_WRITE_ONLY, VX_MEMORY_TYPE_HOST);
    ASSERT_EQ_VX_STATUS(VX_SUCCESS, vxGraphParameterEnqueueReadyRef(graph, 0, (vx_reference *)&image, 1));
    ASSERT_EQ_VX_STATUS(VX_SUCCESS, vxGraphParameterEnqueueReadyRef(graph, 1, (vx_reference *)&scalar, 1));
    ASSERT_EQ_VX_STATUS(VX_SUCCESS, vxGraphParameterEnqueueReadyRef(graph, 2, (vx_reference *)&output, 1)); 
    
    // process the graph
    vxGraphParameterDequeueDoneRef(graph, 0, &dequeue_ref, 1, &num_dequeued_refs);
    vxGraphParameterDequeueDoneRef(graph, 1, &dequeue_ref, 1, &num_dequeued_refs);
    vxGraphParameterDequeueDoneRef(graph, 2, &dequeue_ref, 1, &num_dequeued_refs);

    // change the scalar value and re enqueue/dequeue to see different path taken
    scalar_val = 3;
    vxCopyScalar(scalar, &scalar_val, VX_WRITE_ONLY, VX_MEMORY_TYPE_HOST);
    ASSERT_EQ_VX_STATUS(VX_SUCCESS, vxGraphParameterEnqueueReadyRef(graph, 0, (vx_reference *)&image, 1));
    ASSERT_EQ_VX_STATUS(VX_SUCCESS, vxGraphParameterEnqueueReadyRef(graph, 1, (vx_reference *)&scalar, 1));
    ASSERT_EQ_VX_STATUS(VX_SUCCESS, vxGraphParameterEnqueueReadyRef(graph, 2, (vx_reference *)&output, 1)); 
    
    // process the graph
    vxGraphParameterDequeueDoneRef(graph, 0, &dequeue_ref, 1, &num_dequeued_refs);
    vxGraphParameterDequeueDoneRef(graph, 1, &dequeue_ref, 1, &num_dequeued_refs);
    vxGraphParameterDequeueDoneRef(graph, 2, &dequeue_ref, 1, &num_dequeued_refs);
}


TESTCASE_TESTS(
    SelectMulti,
    testSelectMultiNodePipeline_try_replicate_realsetup
)

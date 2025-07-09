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
Implementation file for the select utilities
*/

#include <vx_internal.h>
#include <vx_select_multi.h>
static vx_node findOptimisableSourceNode(vx_node current_node, vx_reference ref, vx_uint32 *index);
static vx_node findSimpleOutNode(vx_node current_node, vx_reference ref, vx_uint32 *index);
static vx_bool tryOptimiseSelectNode(vx_node s_node, vx_uint8 num_inputs);
static void optimiseBranch(vx_node select_node, const vx_uint32 input_index, const vx_uint32 branch_index);
static void assignMoveOrCopyKernel(vx_node select_node, const vx_uint8 path_option);

vx_status ownGraphEliminateTrivialSelectMultiNodes(vx_graph graph)
{
    /* We don't actually replace the trivial nodes, we just warn the user about them */
    vx_status status = VX_SUCCESS;
    vx_uint32 i;
    for (i = 0; i < graph->num_nodes; i++)
    {
        if (VX_KERNEL_SELECT_MULTI == graph->nodes[i]->kernel->enumeration)
        {
            vx_node node = graph->nodes[i];
            if ((node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_ONE] == node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_TWO]) &&
                (node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_ONE] == node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_THREE]) &&
                (node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_ONE] == node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_FOUR]))
            {
                /* trivial node, replace with a copy node ? */
                VX_PRINT(VX_ZONE_WARNING, "\"true_value\" and \"false_value\" inputs of a Select node have been given the same value. Did you mean to do this?\n");
            }
        }
    }
    return status;
}

/*
 * During graph verification:
 * After copy and move nodes have been processed, and before the topographical sort,
 * select nodes can be processed.
 * If the true_ and false_ value inputs are both virtual and not connected elsewhere,
 * then
 *  the references are replaced, the feeder branches are identified
 *  (at most the preceding 6 nodes, this is the limit of our optimisation),
 *  the execution order changed and the parameters altered
 * else
 *  a warning message is displayed 
 *  "Select node cannot be optimised due to non-virtual inputs / inputs connected elsewhere".
 *
 * If one of the inputs is virtual then the select node is ordered to execute after
 * any other other node connected to that object (by modifying in_nodes & out_nodes),
 * and the VX_KERNEL_MOVE_RB is cast to a vx_reference and set in parameter[VX_KERNEL_SELECT_TRUE_KERNEL]
 * or [VX_KERNEL_SELECT_TRUE_KERNEL].
 * For non-virtual parameter inputs, VX_KERNEL_COPY is cast to a vx_reference and set in parameters
 * [VX_KERNEL_SELECT_TRUE_KERNEL] & [VX_KERNEL_SELECT_TRUE_KERNEL]. Notice that this rather nasty
 * subversion of the parameters array is used simply for performance reasons - it would be possible
 * to pass a reference to a kernel, or to a scalar, but this would incur extra code to get the kernel
 * enum, when it could be simply placed in the parameter list.
 * 
 * Notes about identifying the "feeder branches"
 *  Stop at the first node where an output/bidir is connected elsewhere in the graph, since
 *   "elsewhere" must also execute - this should also catch nodes that feed both branches
 *   This node cannot be included in the branch.
 *  Stop at the first node with a non-virtual output; this node cannot be included in the branch.
 *  Stop at a leaf (obviously!); this node *can* be included in the branch, but if it is a graph
 *  parameter, then the select node operation must be made dependent upon that parameter being
 *  enqueued.
 * 
 * Where we've identified the feeder branches, we have also identified the references that must
 * be queued to allow the select node to run. Where a select node is used in a pipelined graph,
 * the user must ensure that all such inputs are queued before the condition input is available.
 * (Can we get around this somehow?)
 */

/* Find the node that writes the given reference for the current node */
static vx_node findSimpleOutNode(vx_node current_node, vx_reference ref, vx_uint32 *index)
{
    vx_graph graph = current_node->graph;
    vx_uint32 n;
    for (n = 0; n < graph->num_nodes; ++n)
    {
        vx_node node = graph->nodes[n];
        if (node != current_node)
        {
            vx_uint32 out_n;
            /* Check that the current node is in the list of out_nodes */
            for (out_n = 0; out_n < node->obj_desc[0]->num_out_nodes; ++out_n)
            {
                if (node->obj_desc[0]->out_node_id[out_n] == current_node->obj_desc[0]->base.obj_desc_id)
                {
                    vx_uint32 p;
                    /* Check to see if the reference is being written to */
                    for (p = 0; p < node->kernel->signature.num_parameters; ++p)
                    {
                        if ((VX_INPUT != node->kernel->signature.directions[p]) &&
                            ownGraphCheckIsRefMatch(graph, ref, node->parameters[p]))
                        {
                            if (index)
                            {
                                *index = p;
                            }
                            return node;
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

/* Find the node that writes or modifies the given virtual reference,
   has the current_node as it's only out_node, and writes to no other
   parameters.
   Return NULL if no such node is found, or if the reference is not
   virtual.
*/
static vx_node findOptimisableSourceNode(vx_node current_node, vx_reference ref, vx_uint32 *index)
{
    vx_node ret_node = NULL;
    if ((ref != NULL) &&
        ref->is_virtual)
    {
        vx_graph graph = current_node->graph;
        vx_uint32 n;
        vx_uint16 cn_id = current_node->obj_desc[0]->base.obj_desc_id;
        vx_bool found_ref = vx_false_e;
        for (n = 0; n < graph->num_nodes; ++n)
        {
            vx_node node = graph->nodes[n];
            if ((node != current_node) &&                       /* ignore current node */
                (1 == node->obj_desc[0]->num_out_nodes) &&      /* must be only 1 out_node */
                (node->obj_desc[0]->out_node_id[0] == cn_id))   /* and the out node must be the current node */
            {
                vx_uint32 p;
                for (p = 0; p < node->kernel->signature.num_parameters; ++p)
                {
                    if (VX_INPUT != node->kernel->signature.directions[p])
                    {
                        if (ownGraphCheckIsRefMatch(graph, ref, node->parameters[p]))
                        {
                            /* This could be it, so long as there are no other outputs */
                            if (index)
                            {
                                *index = p;
                            }
                            ret_node = node;
                            found_ref = vx_true_e;
                        }
                        else
                        {   /* We've found an output that's not ours, so this node is no good */
                            ret_node = NULL;
                            break;
                        }
                    }
                }
                if (found_ref)
                {
                    /* Terminate the loop if we found the reference,
                       we may have a good node, or it may have been
                       set to NULL because of another output
                    */
                    break;
                }
            }
        }
    }
    return ret_node;
}

/* Figure out if we can optimise the select node.
   It's not possible to optimise the node if:
    The node is replicated and the condition input is replicated.
    either T or F input is non-virtual
    T, F, C, inputs don't all come from different output nodes
    T or F connected to any other input
    T or F connected to a bidirectional on a node with any out node
    other than the select node (i.e. out_nodes > 1)
    T or F originating from a node with other outputs

    We can test for other connected inputs, or other outputs
    by testing the number of output nodes of the originating
    node. It should be either equal to 1. Any bidirectional
    connected node must only have a fan out of 1.

    The function adjusts the graph for the immediately
    preceding nodes if it can be optimised. 
  */
static vx_bool tryOptimiseSelectNode(vx_node s_node, vx_uint8 num_inputs)
{
    vx_bool ret = vx_false_e;
    if ((vx_false_e == tivxFlagIsBitSet(s_node->obj_desc[0]->flags, TIVX_NODE_FLAG_IS_REPLICATED)) ||
        (vx_false_e == tivxFlagIsBitSet(s_node->obj_desc[0]->is_prm_replicated, 1 << VX_KERNEL_SELECT_MULTI_CONDITION_INPUT)))
    {
        vx_reference refOne = s_node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_ONE];
        vx_reference refTwo = s_node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_TWO];
        vx_reference refThree = s_node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_THREE];
        vx_reference refFour = s_node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_FOUR];
        vx_reference refOut = s_node->parameters[VX_KERNEL_SELECT_MULTI_OUTPUT];
        vx_uint32 out_nodeOne_ix = 0;
        vx_uint32 out_nodeTwo_ix = 0;
        vx_node out_nodeOne = findOptimisableSourceNode(s_node, refOne, &out_nodeOne_ix);
        vx_node out_nodeTwo = findOptimisableSourceNode(s_node, refTwo, &out_nodeTwo_ix);
        vx_node out_nodeC = findOptimisableSourceNode(s_node, s_node->parameters[VX_KERNEL_SELECT_MULTI_CONDITION_INPUT], NULL);
        vx_node out_nodeThree = NULL;
        vx_node out_nodeFour = NULL;
        vx_uint32 out_nodeThree_ix = 0;
        vx_uint32 out_nodeFour_ix = 0;
        vx_bool optimize_condition = vx_false_e; 
        if ((out_nodeC != out_nodeTwo) &&
            (out_nodeC != out_nodeOne) &&
            (out_nodeTwo != out_nodeOne) &&
            (NULL != out_nodeOne) &&
            (NULL != out_nodeTwo)) 
        {
            vx_true_e;
        }

        if (num_inputs > 2) // additional checks in case of more than 2 inputs
        {
            optimize_condition = vx_false_e;
            refThree = s_node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_THREE];
            out_nodeThree = findOptimisableSourceNode(s_node, s_node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_THREE], &out_nodeThree_ix);
            if ((out_nodeC != out_nodeThree) &&
                (out_nodeThree != out_nodeTwo) &&
                (out_nodeThree != out_nodeOne) &&
                (NULL != out_nodeThree))
            {
                optimize_condition = vx_true_e;
            }
        }
        if (num_inputs > 3) // additional checks in case of more than 3 inputs
        {
            optimize_condition = vx_false_e;
            refFour = s_node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_FOUR];
            out_nodeFour = findOptimisableSourceNode(s_node, s_node->parameters[VX_KERNEL_SELECT_MULTI_INPUT_FOUR], &out_nodeFour_ix);
            if ((out_nodeC != out_nodeFour) &&
                (out_nodeFour != out_nodeTwo) &&
                (out_nodeFour != out_nodeOne) &&
                (out_nodeFour != out_nodeThree) &&
                (NULL != out_nodeFour))
            {
                optimize_condition = vx_true_e;
            }
        }

        if (optimize_condition)
        {
            /* What if there is a graph parameter on the Select node output ?
               We have a problem, can't optimise it!
            */
            vx_uint32 nix;
            ret = vx_true_e;
            for (nix = 0; nix < s_node->graph->num_params; ++nix)
            {
                if ((s_node->graph->parameters[nix].node == s_node) &&
                    (VX_KERNEL_SELECT_MULTI_OUTPUT == s_node->graph->parameters[nix].index))
                {
                    ret = vx_false_e;
                    break;
                }
            }
            if (ret)
            {
                VX_PRINT(VX_ZONE_INFO, "Branch optimisation of select node\n");
                /* We can optimise, take the first steps and put the
                T & F nodes in place on their node list.
                Note all parameters were initialised to NULL so we
                don't need to specifically terminate the lists.
                */
                s_node->parameters[VX_KERNEL_SELECT_MULTI_BRANCH_ONE] = (vx_reference)out_nodeOne;
                s_node->parameters[VX_KERNEL_SELECT_MULTI_BRANCH_TWO] = (vx_reference)out_nodeTwo;
                /* Set the output of the source nodes to be the output of the select node */
                ownNodeSetParameter(out_nodeOne, out_nodeOne_ix, refOut);
                ownNodeSetParameter(out_nodeTwo, out_nodeTwo_ix, refOut);   

                /* The out nodes of the select node become the out nodes of the source nodes,
                all out nodes removed for the select node */
                out_nodeOne->obj_desc[0]->num_out_nodes = s_node->obj_desc[0]->num_out_nodes;
                out_nodeTwo->obj_desc[0]->num_out_nodes = s_node->obj_desc[0]->num_out_nodes;

                for (nix = 0; nix < s_node->obj_desc[0]->num_out_nodes; ++nix)
                {
                    out_nodeOne->obj_desc[0]->out_node_id[nix] = s_node->obj_desc[0]->out_node_id[nix];
                    out_nodeTwo->obj_desc[0]->out_node_id[nix] = s_node->obj_desc[0]->out_node_id[nix];
                }
                /* If there is a bidirectional parameter involved, we also have to set
                the output reference on the feed node of that */
                out_nodeOne = findSimpleOutNode(out_nodeOne, refOne, &out_nodeOne_ix);
                if (NULL != out_nodeOne)
                {
                    ownNodeSetParameter(out_nodeOne, out_nodeOne_ix, refOut);
                }
                out_nodeTwo = findSimpleOutNode(out_nodeTwo, refTwo, &out_nodeTwo_ix);
                if (NULL != out_nodeTwo)
                {
                    ownNodeSetParameter(out_nodeTwo, out_nodeTwo_ix, refOut);
                }      

                if (num_inputs > 2)
                {
                    s_node->parameters[VX_KERNEL_SELECT_MULTI_BRANCH_THREE] = (vx_reference)out_nodeThree;
                    ownNodeSetParameter(out_nodeThree, out_nodeThree_ix, refOut);

                    /* The out nodes of the select node become the out nodes of the source nodes,
                    all out nodes removed for the select node */
                    out_nodeThree->obj_desc[0]->num_out_nodes = s_node->obj_desc[0]->num_out_nodes;

                    for (nix = 0; nix < s_node->obj_desc[0]->num_out_nodes; ++nix)
                    {
                        out_nodeThree->obj_desc[0]->out_node_id[nix] = s_node->obj_desc[0]->out_node_id[nix];
                    }
                    /* If there is a bidirectional parameter involved, we also have to set
                    the output reference on the feed node of that */
                    out_nodeThree = findSimpleOutNode(out_nodeThree, refThree, &out_nodeThree_ix);
                    if (NULL != out_nodeThree)
                    {
                        ownNodeSetParameter(out_nodeThree, out_nodeThree_ix, refOut);
                    }
                }
                if (num_inputs > 3)
                {
                    s_node->parameters[VX_KERNEL_SELECT_MULTI_BRANCH_FOUR] = (vx_reference)out_nodeFour;
                    ownNodeSetParameter(out_nodeFour, out_nodeFour_ix, refOut);

                    /* The out nodes of the select node become the out nodes of the source nodes,
                    all out nodes removed for the select node */
                    out_nodeFour->obj_desc[0]->num_out_nodes = s_node->obj_desc[0]->num_out_nodes;

                    for (nix = 0; nix < s_node->obj_desc[0]->num_out_nodes; ++nix)
                    {
                        out_nodeFour->obj_desc[0]->out_node_id[nix] = s_node->obj_desc[0]->out_node_id[nix];
                    }
                    /* If there is a bidirectional parameter involved, we also have to set
                    the output reference on the feed node of that */
                    out_nodeFour = findSimpleOutNode(out_nodeFour, refFour, &out_nodeFour_ix);
                    if (NULL != out_nodeFour)
                    {
                        ownNodeSetParameter(out_nodeFour, out_nodeFour_ix, refOut);
                    }
                }
                /* set outgoing nodes of select node to 0 */
                for (nix = 0; nix < s_node->obj_desc[0]->num_out_nodes; ++nix)
                {
                    s_node->obj_desc[0]->out_node_id[nix] = 0;
                }
                s_node->obj_desc[0]->num_out_nodes = 0;
            }        
        }
    }
    return ret;
}

/* Try to optimise an input branch of the select node.
   Trace back through the graph finding nodes that do not
   need to be executed if the given input of the select node
   is not required, and fill in the list with these nodes.
   This method can be expanded, it is conservative in order to
   a) Keep it simple, and
   b) Not make any mistakes
   Currently it does not try to optimise the inputs of any
   * nodes with other outputs (even if they merge back to the same branch)
   A stack is employed to avoid recursion.
   Tracing stops when the maximum number of nodes in the list is reached,
   regardless of whether or not the best path (i.e. the most expensive)
   is being eliminated.
*/
static void optimiseBranch(vx_node select_node, const vx_uint32 input_index, const vx_uint32 branch_index)
{
    vx_node node_stack[VX_KERNEL_SELECT_MAX_NODES_IN_BRANCH];
    vx_uint32 index = 0;
    vx_uint32 sptr = 0;
    /* Stack the first node. We already know this may be optimised */
    node_stack[sptr++] = (vx_node)select_node->parameters[branch_index + index++];
    while (sptr)
    {
        vx_node node = node_stack[--sptr];
        /* Now cycle through all the input references to find candidate nodes.
           If their source nodes can be optimised, add them to the list and stack them */
        vx_uint32 pindex;
        for (pindex = 0; pindex < node->kernel->signature.num_parameters; ++pindex)
        {
            if ((VX_KERNEL_SELECT_MAX_NODES_IN_BRANCH == sptr) ||
                (VX_KERNEL_SELECT_MAX_NODES_IN_BRANCH == index))
            {
                /* We've either filled the list or run out of stack */
                VX_PRINT(VX_ZONE_INFO, "Select branch optimisation terminated with sptr=%d, index=%d\n", sptr, index);
                break;
            }
            if (VX_OUTPUT != node->kernel->signature.directions[pindex])
            {
                /* See if the source of this parameter can be optimised.
                */
                vx_node src_node = findOptimisableSourceNode(node, node->parameters[pindex], NULL);
                if (NULL != src_node)
                {
                    /* Source node can be optimised, but we need to check that we don't already have it on the list */
                    vx_uint32 ix;
                    vx_bool not_on_list = vx_true_e;
                    for (ix = 0; ix < index; ++ix)
                    {
                        if (select_node->parameters[branch_index + ix] == (vx_reference)src_node)
                        {
                            not_on_list = vx_false_e;
                            break;
                        }
                    }
                    if (not_on_list)
                    {
                        select_node->parameters[branch_index + index++] = (vx_reference)src_node;
                        node_stack[sptr++] = src_node;
                    }
                }
            }
        }
    }
    /* Make execution of the last identified node dependent upon the select_node execution */
    vx_node node = (vx_node)select_node->parameters[branch_index + index - 1];
    node->obj_desc[0]->in_node_id[node->obj_desc[0]->num_in_nodes++] = select_node->obj_desc[0]->base.obj_desc_id;
    select_node->obj_desc[0]->out_node_id[select_node->obj_desc[0]->num_out_nodes++] = node->obj_desc[0]->base.obj_desc_id;
    VX_PRINT(VX_ZONE_INFO, "%d node(s) of one branch Select node could be suppressed: execution of %s dependent upon %s\n", index, node->base.name, select_node->base.name);
}

/* Assign either a Move or a Copy kernel for the given input */
static void assignMoveOrCopyKernel(vx_node select_node, const vx_uint8 path_option)
{
    vx_reference ref = select_node->parameters[path_option];
    vx_enum kernel = VX_KERNEL_COPY;
    /* now let's see if we can use the Move kernel.
    our simple rule is that the reference is not 
    connected to an input elsewhere, another is that
    it is a sub-object. We check that the MOVE kernel
    validates correctly at this point! */
    if (ref->is_virtual)
    {
        vx_graph graph = select_node->graph;
        vx_uint32 n;
        /* Assume we can use the Move kernel */
        kernel = VX_KERNEL_MOVE;
        vx_reference mparams[2] = {ref, select_node->parameters[VX_KERNEL_SELECT_MULTI_OUTPUT]};
        if (0 == (ref->kernel_callback)(kernel, vx_true_e, ref, select_node->parameters[VX_KERNEL_SELECT_MULTI_OUTPUT]))
        {
            /* Move node validated OK */
            for (n = 0; n < graph->num_nodes; ++n)
            {
                vx_node node = graph->nodes[n];
                if (node != select_node)
                {
                    vx_uint32 p;
                    for (p = 0; p < node->kernel->signature.num_parameters; ++p)
                    {
                        if ((VX_INPUT == node->kernel->signature.directions[p]) &&
                        ownGraphCheckIsRefMatch(graph, ref, node->parameters[p]))
                        {
                            /* we have found a reason to not use the Move kernel */
                            kernel = VX_KERNEL_COPY;
                            break;
                        }
                    }
                }
                if (VX_KERNEL_COPY == kernel)
                {
                    break;
                }
            }
        }
        else
        {
            kernel = VX_KERNEL_COPY;
        }
    }
    if  (VX_KERNEL_COPY == kernel)
    {
        VX_PRINT(VX_ZONE_INFO, "The \"%s_Value\" input of the Select kernel is not virtual or is used elsewhere and so cannot be optimised\n", path_option);
    }
    vx_uint8 kernel_option[4U] = {VX_KERNEL_SELECT_MULTI_KERNEL_ONE, 
                                  VX_KERNEL_SELECT_MULTI_KERNEL_TWO,
                                  VX_KERNEL_SELECT_MULTI_KERNEL_THREE,
                                  VX_KERNEL_SELECT_MULTI_KERNEL_FOUR}; // 4 aa in 4 possible pathes to be taken

    select_node->parameters[kernel_option[path_option]] = (vx_reference)(uintptr_t)kernel;
}

/*
    This function should be called after Copy and Move
    nodes have been processed.
*/
vx_status ownGraphProcessSelectMultiNodes(vx_graph graph)
{
    vx_status status = VX_SUCCESS;
    vx_uint32 n;
    /* We can just process select nodes one by one. */
    for (n = 0; n < graph->num_nodes; n++)
    {
        if (VX_KERNEL_SELECT_MULTI == graph->nodes[n]->kernel->enumeration)
        {
            vx_node select_node = graph->nodes[n];
            /* get the amount of inputs that can be selected from the node */
            vx_uint8 num_inputs = 0;
            vxCopyScalar(select_node->parameters[VX_KERNEL_SELECT_MULTI_NUM_INPUTS], &num_inputs, VX_READ_ONLY, VX_MEMORY_TYPE_HOST);
            
            /* Zero out the parameters array as we are going to use more than usual.
               Note that VX_KERNEL_SELECT_MAX_PARAM_USED is guaranteed to be less
               than TIVX_KERNEL_MAX_PARAMS by the check in the vx_select.h
            */
            vx_uint32 i;
            for (i = VX_KERNEL_SELECT_MULTI_OUTPUT + 1; i < VX_KERNEL_SELECT_MULTI_MAX_PARAM_USED; ++i)
            {
                select_node->parameters[i] = NULL;
            }
            /* There are several cases:
                Special case where the node is replicated:
                    If the condition input is replicated, we can't do
                    any optimisations of the feeder branches.
                Both inputs virtual, distinct, and not connected elsewhere:
                    Here we can have early execution of the
                    Select node, and no copies or moves need
                    done. The output of both feeder node is
                    set to be the same reference as the output
                    of the Select node, and that output is
                    set to NULL (to indicate an optimised kernel).
                    Branches are traced back at maximum six nodes
                    and at run time the Select node can mark the
                    un-required nodes as executed, and queue the
                    required nodes for execution.
                    Both True and False inputs of the select node
                    are removed, so it is dependent only on the
                    condition being available. The nodes following
                    the Select node are made dependent upon the
                    branches being executed.
                Both inputs non-virtual:
                    no optimisation possible, use Copy kernel
                    for both True and False inputs
                True input virtual, False input non-virtual:
                    We use a Move kernel for the True input,
                    a Copy kernel for the False input.
                False input virtual, True input non-virtual:
                    We use a Move kernel for the False input,
                    a Copy kernel for the True input
                NOTE: In order to use the Move kernel, the select
                node must be the last to be executed of all nodes
                connected to the reference, and none of them can be
                Move node themselves, or we have a simpler rule that
                the reference is not connected to any other inputs.
            */
            if (tryOptimiseSelectNode(select_node, num_inputs))
            {
                /* We have done the first (simplest) stage of optimisation,
                   now see if we can trace further back on each branch
                */
                optimiseBranch(select_node, VX_KERNEL_SELECT_MULTI_INPUT_ONE, VX_KERNEL_SELECT_MULTI_BRANCH_ONE);
                optimiseBranch(select_node, VX_KERNEL_SELECT_MULTI_INPUT_TWO, VX_KERNEL_SELECT_MULTI_BRANCH_TWO);
                if (num_inputs > 2)
                {
                    optimiseBranch(select_node, VX_KERNEL_SELECT_MULTI_INPUT_THREE, VX_KERNEL_SELECT_MULTI_BRANCH_THREE);
                }
                if (num_inputs > 3)
                {
                    optimiseBranch(select_node, VX_KERNEL_SELECT_MULTI_INPUT_FOUR, VX_KERNEL_SELECT_MULTI_BRANCH_FOUR);
                }
                /*
                   Finally, we make the select node dependent
                   only on the node providing the select input
                */
                /* Now we make this node execution dependent upon only the condition input */
                vx_node out_nodeOne = (vx_node)select_node->parameters[VX_KERNEL_SELECT_MULTI_BRANCH_ONE];
                vx_node out_nodeTwo = (vx_node)select_node->parameters[VX_KERNEL_SELECT_MULTI_BRANCH_TWO];
                for (i = 0; i < out_nodeOne->obj_desc[0]->num_out_nodes; ++i)
                {
                    if (select_node->obj_desc[0]->base.obj_desc_id == out_nodeOne->obj_desc[0]->out_node_id[i])
                    {
                        out_nodeOne->obj_desc[0]->out_node_id[i] = out_nodeOne->obj_desc[0]->out_node_id[--(out_nodeOne->obj_desc[0]->num_out_nodes)];
                    }
                }
                for (i = 0; i < out_nodeTwo->obj_desc[0]->num_out_nodes; ++i)
                {
                    if (select_node->obj_desc[0]->base.obj_desc_id == out_nodeTwo->obj_desc[0]->out_node_id[i])
                    {
                        out_nodeTwo->obj_desc[0]->out_node_id[i] = out_nodeTwo->obj_desc[0]->out_node_id[--(out_nodeTwo->obj_desc[0]->num_out_nodes)];
                    }
                }
                if (num_inputs > 2)
                {
                    vx_node out_nodeThree = (vx_node)select_node->parameters[VX_KERNEL_SELECT_MULTI_BRANCH_THREE];
                    for (i = 0; i < out_nodeThree->obj_desc[0]->num_out_nodes; ++i)
                    {
                        if (select_node->obj_desc[0]->base.obj_desc_id == out_nodeThree->obj_desc[0]->out_node_id[i])
                        {
                            out_nodeThree->obj_desc[0]->out_node_id[i] = out_nodeThree->obj_desc[0]->out_node_id[--(out_nodeThree->obj_desc[0]->num_out_nodes)];
                        }
                    }
                }
                if (num_inputs > 3)
                {
                    vx_node out_nodeFour = (vx_node)select_node->parameters[VX_KERNEL_SELECT_MULTI_BRANCH_FOUR];
                    for (i = 0; i < out_nodeFour->obj_desc[0]->num_out_nodes; ++i)
                    {
                        if (select_node->obj_desc[0]->base.obj_desc_id == out_nodeFour->obj_desc[0]->out_node_id[i])
                        {
                            out_nodeFour->obj_desc[0]->out_node_id[i] = out_nodeFour->obj_desc[0]->out_node_id[--(out_nodeFour->obj_desc[0]->num_out_nodes)];
                        }
                    }
                }
                vx_node c_node = findSimpleOutNode(select_node, select_node->parameters[VX_KERNEL_SELECT_MULTI_CONDITION_INPUT], NULL);
                if (NULL != c_node)
                {
                    select_node->obj_desc[0]->num_in_nodes = 1;
                    select_node->obj_desc[0]->in_node_id[0] = c_node->obj_desc[0]->base.obj_desc_id;
                }
                else
                {
                    select_node->obj_desc[0]->num_in_nodes = 0;
                }
            }
            else
            {
                VX_PRINT(VX_ZONE_INFO, "The Select kernel inputs are not distinct and virtual\n");
                /* Optimise with Move kernels where possible */
                assignMoveOrCopyKernel(select_node, 1);
                assignMoveOrCopyKernel(select_node, 2);
                if( num_inputs > 2)
                {
                    assignMoveOrCopyKernel(select_node, 3);
                }
                if( num_inputs > 3)
                {
                    assignMoveOrCopyKernel(select_node, 4); 
                }
            }
        }
    }
    return status;
}

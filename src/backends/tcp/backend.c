/*
 * This file is part of the LAIK library.
 * Copyright (c) 2018 Alexander Kurtz <alexander@kurtz.be>
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 or later.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>           // for g_malloc0_n, g_malloc, g_autoptr, g_new0
#include <laik-internal.h>  // for _Laik_Data, _Laik_Group, redTOp, _Laik_Ma...
#include <laik.h>           // for Laik_Data, Laik_Group, Laik_Mapping, Laik...
#include <stdbool.h>        // for false, true, bool
#include <stdint.h>         // for int64_t
#include <stdio.h>          // for NULL, size_t, sprintf
#include <string.h>         // for memcpy
#include "async.h"          // for laik_tcp_async_new, laik_tcp_async_wait
#include "config.h"         // for laik_tcp_config, Laik_Tcp_Config, Laik_Tc...
#include "debug.h"          // for laik_tcp_always
#include "errors.h"         // for laik_tcp_errors_push, laik_tcp_errors_pre...
#include "mpi.h"            // for MPI_Comm, MPI_Datatype, MPI_COMM_WORLD

/* Internal structs */

typedef struct {
    const Laik_Data*        data;
    const Laik_Transition*  transition;
    const Laik_MappingList* input_list;
} Laik_Tcp_Backend_AsyncSendInfo;

typedef struct {
    const Laik_Group* group;
    MPI_Comm          communicator;
    MPI_Datatype      mpi_type;
    size_t            elements;
    const TaskGroup*  output_group;
    const void*       input_buffer;
} Laik_Tcp_Backend_AsyncReduceInfo;

/* Internal functions */

static void laik_tcp_backend_push_code (Laik_Tcp_Errors* errors, int code) {
    laik_tcp_always (errors);

    if (code == MPI_SUCCESS) {
        return;
    }

    char message[MPI_MAX_ERROR_STRING];
    int length;

    if (MPI_Error_string (code, message, &length) == MPI_SUCCESS) {
        laik_tcp_errors_push (errors, __func__, 0, "An MPI operation failed, details below\n%s", message);
    } else {
        laik_tcp_errors_push (errors, __func__, 0, "An MPI operation failed and MPI_Error_string() failed to produce a detailed error message");
    }
}

__attribute__ ((warn_unused_result))
static MPI_Datatype laik_tcp_backend_get_mpi_type (const Laik_Data* data, Laik_Tcp_Errors* errors) {
    laik_tcp_always (data);
    laik_tcp_always (errors);

    if (data->type == laik_Double) {
        return MPI_DOUBLE;
    }

    if (data->type == laik_Float) {
        return MPI_FLOAT;
    }

    laik_tcp_errors_push (errors, __func__, 0, "Unknown LAIK type: %s", data->type->name);
    return 0;
}

__attribute__ ((warn_unused_result))
static bool laik_tcp_backend_task_group_contains (const TaskGroup* group, const int task) {
    // A null pointer shall mean "all tasks", so return a positive result
    if (group == NULL) {
        return true;
    }

    // Check if the the specified task is a member of the task group
    for (int i = 0; i < group->count; i++) {
        if (group->task[i] == task) {
            return true;
        }
    }

    // No luck, return a negative result
    return false;
}

static void laik_tcp_backend_receive
    ( const Laik_Data* data
    , Laik_Mapping* output
    , const Laik_Slice slice
    , const int sender
    , Laik_Tcp_Errors* errors
    )
{
    laik_tcp_always (data);
    laik_tcp_always (output);
    laik_tcp_always (errors);

    // Calculate some variables we are going to need later on
    const Laik_Group* group    = data->activePartitioning->group;
    const MPI_Comm    comm     = *((MPI_Comm*) group->backend_data);
    const size_t      elements = laik_slice_size(&slice);
    const size_t      bytes    = elements * data->elemsize;
    g_autofree void*  buffer   = g_malloc (bytes);
    Laik_Index        start    = slice.from;

    // Make sure we aren't talking to ourselves
    laik_tcp_always (group->myid != sender);

    // Make sure the mapping is ready to use
    if (!output->base) {
        laik_allocateMap (output, data->stat);
    }

    // Make sure the mapping supports deserialization
    laik_tcp_always (output->layout->unpack);

    // Determine the MPI data type
    const MPI_Datatype mpi_type = laik_tcp_backend_get_mpi_type (data, errors);
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to map LAIK data type to MPI data type");
        return;
    }

    // Receive the data
    MPI_Status status;
    laik_tcp_backend_push_code (errors, MPI_Recv (buffer, elements, mpi_type, sender, 10, comm, &status));
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 1, "Failed to receive MPI message from task %d", sender);
        return;
    }

    // Determine how many logical elements we have received
    int received;
    laik_tcp_backend_push_code (errors, MPI_Get_count (&status, mpi_type, &received));
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 2, "Failed to determine how many elements were received");
        return;
    }

    // Check that we received exactly as many elements as expected
    if (received != (ssize_t) elements) {
        laik_tcp_errors_push (errors, __func__, 3, "Received %d elements, but expected %zu elements", received, elements);
        return;
    }

    // Unpack the data
    int unpacked = output->layout->unpack (output, &slice, &start, buffer, bytes);

    // Check that we unpacked exactly as many elements as expected
    if (unpacked != (ssize_t) elements) {
        laik_tcp_errors_push (errors, __func__, 4, "Unpacked %d elements, but expected %zu elements", unpacked, elements);
        return;
    }

    // Update the statistics
    if (data->stat) {
        data->stat->msgRecvCount++;
        data->stat->byteRecvCount += bytes;
    }
}

static void laik_tcp_backend_send
    ( const Laik_Data* data
    , const Laik_Mapping* input
    , const Laik_Slice slice
    , const int receiver
    , Laik_Tcp_Errors* errors
    )
{
    laik_tcp_always (data);
    laik_tcp_always (input);
    laik_tcp_always (errors);

    // Calculate some variables we are going to need later on
    const Laik_Group* group    = data->activePartitioning->group;
    const MPI_Comm    comm     = *((MPI_Comm*) group->backend_data);
    const size_t      elements = laik_slice_size(&slice);
    const size_t      bytes    = elements * data->elemsize;
    g_autofree void*  buffer   = g_malloc (bytes);
    Laik_Index        start    = slice.from;

    // Make sure we aren't talking to ourselves
    laik_tcp_always (group->myid != receiver);

    // Make sure the mapping is ready to use
    laik_tcp_always (input->base);

    // Make sure the mapping supports serialization
    laik_tcp_always (input->layout->pack);

    // Determine the MPI data type
    const MPI_Datatype mpi_type = laik_tcp_backend_get_mpi_type (data, errors);
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to map LAIK data type to MPI data type");
        return;
    }

    // Pack the data
    int packed = input->layout->pack (input, &slice, &start, buffer, bytes);
    if (packed != (ssize_t) elements) {
        laik_tcp_errors_push (errors, __func__, 1, "Packed %d elements, but expected %zu", packed, elements);
        return;
    }

    // Send the data
    laik_tcp_backend_push_code (errors, MPI_Send (buffer, elements, mpi_type, receiver, 10, comm));
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 1, "Failed to send MPI message to task %d", receiver);
        return;
    }

    // Update the statistics
    if (data->stat) {
        data->stat->msgSendCount++;
        data->stat->byteSendCount += bytes;
    }
}

__attribute__ ((warn_unused_result))
static bool laik_tcp_backend_native_reduce
    ( const MPI_Comm communicator
    , const MPI_Datatype mpi_type
    , const TaskGroup* input_group
    , const TaskGroup* output_group
    , const Laik_ReductionOperation op
    , const void* input_buffer
    , void* output_buffer
    , const int elements
    , Laik_Tcp_Errors* errors
    )
{
    laik_tcp_always (errors);

    // Get the configuration
    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    // If native reductions are disabled, return immediatly
    if (!config->backend_native_reduce) {
        return false;
    }

    MPI_Op mpi_operation;
    switch (op) {
        case LAIK_RO_Sum:
            mpi_operation = MPI_SUM;
            break;
        default:
            return false;
    }

    if (input_group == NULL && output_group == NULL) {
        if (input_buffer == output_buffer) {
            input_buffer = MPI_IN_PLACE;
        }

        laik_tcp_backend_push_code (errors, MPI_Allreduce (input_buffer, output_buffer, elements, mpi_type, mpi_operation, communicator));
        if (laik_tcp_errors_present (errors)) {
            laik_tcp_errors_push (errors, __func__, 1, "Failed to run MPI_Allreduce");
            return false;
        }

        return true;
    } else if (input_group == NULL && output_group && output_group->count == 1) {
        if (input_buffer == output_buffer) {
            input_buffer = MPI_IN_PLACE;
        }

        laik_tcp_backend_push_code (errors, MPI_Reduce (input_buffer, output_buffer, elements, mpi_type, mpi_operation, output_group->task[0], communicator));
        if (laik_tcp_errors_present (errors)) {
            laik_tcp_errors_push (errors, __func__, 2, "Failed to run MPI_Reduce");
            return false;
        }

        return true;
    } else {
        return false;
    }
}

static void* laik_tcp_backend_run_async_reduces (void* data, Laik_Tcp_Errors* errors) {
    laik_tcp_always (data);
    laik_tcp_always (errors);

    Laik_Tcp_Backend_AsyncReduceInfo* info = data;

    // If we have input to contribute to the reduction...
    if (info->input_buffer) {
        // ... send it to all tasks ...
        for (int receiver = 0; receiver < info->group->size; receiver++) {
            // .. which are in the output group, but not to ourselves.
            if (receiver != info->group->myid && laik_tcp_backend_task_group_contains (info->output_group, receiver)) {
                laik_tcp_backend_push_code (errors, MPI_Send (info->input_buffer, info->elements, info->mpi_type, receiver, 11, info->communicator));
                if (laik_tcp_errors_present (errors)) {
                    laik_tcp_errors_push (errors, __func__, 0, "Failed to send MPI message to output task %d", receiver);
                    return NULL;
                }
            }
        }
    }

    return NULL;
}

static void laik_tcp_backend_reduce
    ( const Laik_Data* data
    , const TaskGroup* input_group
    , const TaskGroup* output_group
    , const Laik_Mapping* input_mapping
    , Laik_Mapping* output_mapping
    , const struct redTOp* op
    , Laik_Tcp_Errors* errors
    )
{
    laik_tcp_always (data);
    laik_tcp_always (op);
    laik_tcp_always (errors);

    // Get the configuration
    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    laik_tcp_always (data->space->dims == 1);

    // Determine the MPI data type
    const MPI_Datatype mpi_type = laik_tcp_backend_get_mpi_type (data, errors);
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to map LAIK data type to MPI data type");
        return;
    }

    const Laik_Group*   group        = data->activePartitioning->group;
    const MPI_Comm      communicator = *((MPI_Comm*) group->backend_data);
    const size_t        elements     = op->slc.to.i[0] - op->slc.from.i[0];
    const size_t        bytes        = elements * data->elemsize;

    laik_tcp_always (group->myid >= 0);

    char* input_buffer = NULL;
    if (input_mapping) {
        laik_tcp_always (op->slc.from.i[0] >= input_mapping->requiredSlice.from.i[0]);
        input_buffer = input_mapping->base + (op->slc.from.i[0] - input_mapping->requiredSlice.from.i[0]) * data->elemsize;
    }

    char* output_buffer = NULL;
    if (output_mapping) {
        if (output_mapping->base == NULL) {
            laik_allocateMap (output_mapping, data->stat);
        }

        laik_tcp_always (op->slc.from.i[0] >= output_mapping->requiredSlice.from.i[0]);

        output_buffer = output_mapping->base + (op->slc.from.i[0] - output_mapping->requiredSlice.from.i[0]) * data->elemsize;
    }

    const bool native_reduce = laik_tcp_backend_native_reduce (communicator, mpi_type, input_group, output_group, op->redOp, input_buffer, output_buffer, elements, errors);
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 1, "Failed to do native reduce");
        return;
    }

    if (!native_reduce) {
        if (config->backend_peer_reduce) {
            // Since input_buffer and output_buffer may be the same, dup it here
            g_autofree const void* input_buffer_copy = g_memdup (input_buffer, bytes);

            // Asynchronously send the reduction input to all output tasks
            Laik_Tcp_Backend_AsyncReduceInfo info = {
                .group        = group,
                .communicator = communicator,
                .mpi_type     = mpi_type,
                .elements     = elements,
                .output_group = output_group,
                .input_buffer = input_buffer_copy,
            };
            Laik_Tcp_Async* async = laik_tcp_async_new (laik_tcp_backend_run_async_reduces, &info);

            // If we are an output task, receive the input from all input tasks
            if (output_buffer) {
                // Allocate a buffer where MPI_Recv can store the reduce data from
                // the other tasks. Using a smaller buffer would mean we have to do
                // do multiple MPI_Recv calls, so we essentially chose speed over
                // memory consumption here.
                g_autofree void* receive_buffer = g_malloc (bytes);

                // Keep track of whether we already have a base element and can
                // therefore do a reduce operation with a new element.
                bool have_base_element = false;

                // Iterate over our the tasks in our group in order...
                for (int sender = 0; sender < group->size; sender++) {
                    // ... and include the data from all tasks which have been
                    // marked as input tasks for this reduction.
                    if (laik_tcp_backend_task_group_contains (input_group, sender)) {
                        if (sender == group->myid) {
                            if (have_base_element) {
                                data->type->reduce (output_buffer, output_buffer, input_buffer_copy, elements, op->redOp);
                            } else {
                                memcpy (output_buffer, input_buffer_copy, bytes);
                                have_base_element = true;
                            }
                        } else {
                            if (have_base_element) {
                                MPI_Status status;

                                laik_tcp_backend_push_code (errors, MPI_Recv (receive_buffer, elements, mpi_type, sender, 11, communicator, &status));
                                if (laik_tcp_errors_present (errors)) {
                                    laik_tcp_errors_push (errors, __func__, 2, "Failed to receive reduction input from task %d", sender);
                                    return;
                                }

                                data->type->reduce (output_buffer, output_buffer, receive_buffer, elements, op->redOp);
                            } else {
                                MPI_Status status;

                                laik_tcp_backend_push_code (errors, MPI_Recv (output_buffer, elements, mpi_type, sender, 11, communicator, &status));
                                if (laik_tcp_errors_present (errors)) {
                                    laik_tcp_errors_push (errors, __func__, 3, "Failed to receive reduction input from task %d", sender);
                                    return;
                                }

                                have_base_element = true;
                            }
                        }
                    }
                }

                // Check that we wrote to the result buffer at least once
                laik_tcp_always (have_base_element);
            }

            // Wait for the asynchronous send operations to complete
            __attribute__ ((unused)) void* result = laik_tcp_async_wait (async, errors);
            if (laik_tcp_errors_present (errors)) {
                laik_tcp_errors_push (errors, __func__, 4, "Asynchronous send of reduction input to all tasks in the output group failed");
                return;
            }
        } else {
            // Let one task from the output group to do the reduction for everybody.
            int reduction_task = -1;
            for (int task = 0; task < group->size; task++) {
                if (laik_tcp_backend_task_group_contains (output_group, task)) {
                    reduction_task = task;
                    break;
                }
            }
            laik_tcp_always (reduction_task >= 0);

            if (reduction_task == group->myid) {
                // Allocate a buffer where MPI_Recv can store the reduce data from
                // the other tasks. Using a smaller buffer would mean we have to do
                // do multiple MPI_Recv calls, so we essentially chose speed over
                // memory consumption here.
                g_autofree void* receive_buffer = g_malloc (bytes);

                // Allocate a buffer where we can incrementally construct the result
                // of the reduction. We can't use the output buffer here, since it
                // may point to the same memory as the input buffer and we don't
                // want to enforce that the local data must always the first element
                // in the reduction.
                g_autofree void* result_buffer = g_malloc (bytes);

                // Keep track of whether we already have a base element and can
                // therefore do a reduce operation with a new element.
                bool have_base_element = false;

                // Iterate over our the tasks in our group in order...
                for (int sender = 0; sender < group->size; sender++) {
                    // ... and include the data from all tasks which have been
                    // marked as input tasks for this reduction.
                    if (laik_tcp_backend_task_group_contains (input_group, sender)) {
                        if (sender == group->myid) {
                            if (have_base_element) {
                                data->type->reduce (result_buffer, result_buffer, input_buffer, elements, op->redOp);
                            } else {
                                memcpy (result_buffer, input_buffer, bytes);
                                have_base_element = true;
                            }
                        } else {
                            if (have_base_element) {
                                MPI_Status status;

                                laik_tcp_backend_push_code (errors, MPI_Recv (receive_buffer, elements, mpi_type, sender, 11, communicator, &status));
                                if (laik_tcp_errors_present (errors)) {
                                    laik_tcp_errors_push (errors, __func__, 5, "Failed to receive reduction input from task %d", sender);
                                    return;
                                }

                                data->type->reduce (result_buffer, result_buffer, receive_buffer, elements, op->redOp);
                            } else {
                                MPI_Status status;

                                laik_tcp_backend_push_code (errors, MPI_Recv (result_buffer, elements, mpi_type, sender, 11, communicator, &status));
                                if (laik_tcp_errors_present (errors)) {
                                    laik_tcp_errors_push (errors, __func__, 6, "Failed to receive reduction input from task %d", sender);
                                    return;
                                }

                                have_base_element = true;
                            }
                        }
                    }
                }

                // Check that we wrote to the result buffer at least once and
                // then copy it to the output buffer.
                laik_tcp_always (have_base_element);
                memcpy (output_buffer, result_buffer, bytes);

                // Send the reduction result to all the tasks in the output group.
                for (int receiver = 0; receiver < group->size; receiver++) {
                    if (receiver != group->myid && laik_tcp_backend_task_group_contains (output_group, receiver)) {
                        laik_tcp_backend_push_code (errors, MPI_Send (output_buffer, elements, mpi_type, receiver, 12, communicator));
                        if (laik_tcp_errors_present (errors)) {
                            laik_tcp_errors_push (errors, __func__, 7, "Failed to send out reduction result back to task %d", reduction_task);
                            return;
                        }
                    }
                }
            } else {
                if (input_mapping) {
                    laik_tcp_backend_push_code (errors, MPI_Send (input_buffer, elements, mpi_type, reduction_task, 11, communicator));
                    if (laik_tcp_errors_present (errors)) {
                        laik_tcp_errors_push (errors, __func__, 8, "Failed to send MPI message to reduction task %d", reduction_task);
                        return;
                    }
                }

                if (output_mapping) {
                    MPI_Status status;
                    laik_tcp_backend_push_code (errors, MPI_Recv (output_buffer, elements, mpi_type, reduction_task, 12, communicator, &status));
                    if (laik_tcp_errors_present (errors)) {
                        laik_tcp_errors_push (errors, __func__, 9, "Failed to receive MPI message from reduction task %d", reduction_task);
                        return;
                    }
                }
            }
        }
    }

    // Update the statistics
    if (data->stat) {
        data->stat->msgReduceCount++;
        data->stat->byteReduceCount += bytes;
    }
}

static void* laik_tcp_backend_run_async_sends (void* data, Laik_Tcp_Errors* errors) {
    laik_tcp_always (data);
    laik_tcp_always (errors);

    const Laik_Tcp_Backend_AsyncSendInfo* info = data;

    for (int i = 0; i < info->transition->sendCount; i++) {
        const struct sendTOp* op = info->transition->send + i;

        laik_tcp_always (info->input_list);
        laik_tcp_always (op->mapNo >= 0 && op->mapNo < info->input_list->count);

        laik_tcp_backend_send (info->data, info->input_list->map + op->mapNo, op->slc, op->toTask, errors);
        if (laik_tcp_errors_present (errors)) {
            laik_tcp_errors_push (errors, __func__, 1, "Send operation failed");
            return NULL;
        }
    }

    return NULL;
}

/* API functions */

static void laik_tcp_backend_exec (Laik_ActionSeq* as)
{
    laik_tcp_always (as);

    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();
    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

    // we only support 1 transition exec action
    assert(as->actionCount == 1);
    assert(as->action[0].type == LAIK_AT_TExec);
    Laik_TransitionContext* tc = as->context[0];
    Laik_Data* data = tc->data;
    Laik_Transition* transition = tc->transition;
    Laik_MappingList* input_list = tc->fromList;
    Laik_MappingList* output_list = tc->toList;

    const Laik_Group* group = data->activePartitioning->group;

    // Handle the reduce operations
    for (int i = 0; i < transition->redCount; i++) {
        const struct redTOp* op = transition->red + i;

        TaskGroup* input_group = NULL;
        if (op->inputGroup >= 0) {
            laik_tcp_always (op->inputGroup < transition->subgroupCount);
            input_group = transition->subgroup + op->inputGroup;
        }

        TaskGroup* output_group = NULL;
        if (op->outputGroup >= 0) {
            laik_tcp_always (op->outputGroup < transition->subgroupCount);
            output_group = transition->subgroup + op->outputGroup;
        }

        Laik_Mapping* input_mapping = NULL;
        if (laik_tcp_backend_task_group_contains (input_group, group->myid)) {
            laik_tcp_always (input_list);
            laik_tcp_always (op->myInputMapNo  >= 0 && op->myInputMapNo  < input_list->count);
            input_mapping = input_list->map + op->myInputMapNo;
        }

        Laik_Mapping* output_mapping = NULL;
        if (laik_tcp_backend_task_group_contains (output_group, group->myid)) {
            laik_tcp_always (output_list);
            laik_tcp_always (op->myOutputMapNo >= 0 && op->myOutputMapNo < output_list->count);
            output_mapping = output_list->map + op->myOutputMapNo;
        }

        laik_tcp_backend_reduce (data, input_group, output_group, input_mapping, output_mapping, op, errors);
        if (laik_tcp_errors_present (errors)) {
            laik_tcp_errors_push (errors, __func__, 0, "Reduce operation failed");
            laik_tcp_errors_abort (errors);
        }
    }

    // Decide whether we want asynchronous sends
    if (config->backend_async_send) {
        // Start an asynchrounous operation which will do all the send operations
        Laik_Tcp_Backend_AsyncSendInfo info = {
            .data       = data,
            .transition = transition,
            .input_list = input_list,
        };
        Laik_Tcp_Async* async = laik_tcp_async_new (laik_tcp_backend_run_async_sends, &info);

        // Handle the receive operations
        for (int i = 0; i < transition->recvCount; i++) {
            const struct recvTOp* op = transition->recv + i;

            laik_tcp_always (output_list);
            laik_tcp_always (op->mapNo >= 0 && op->mapNo < output_list->count);

            laik_tcp_backend_receive (data, output_list->map + op->mapNo, op->slc, op->fromTask, errors);
            if (laik_tcp_errors_present (errors)) {
                laik_tcp_errors_push (errors, __func__, 1, "Receive operation failed");
                laik_tcp_errors_abort (errors);
            }
        }

        // Wait for the asynchronous send operations to complete
        __attribute__ ((unused)) void* result = laik_tcp_async_wait (async, errors);
        if (laik_tcp_errors_present (errors)) {
            laik_tcp_errors_push (errors, __func__, 2, "Asynchronous send operation failed");
            laik_tcp_errors_abort (errors);
        }
    } else {
        // Iterate over all the task in group order
        for (int sender = 0; sender < group->size; sender++) {
            if (sender == group->myid) {
                // It's our turn to send data, so do all our send operations.
                for (int i = 0; i < transition->sendCount; i++) {
                    const struct sendTOp* op = transition->send + i;

                    laik_tcp_always (input_list);
                    laik_tcp_always (op->mapNo >= 0 && op->mapNo < input_list->count);

                    laik_tcp_backend_send (data, input_list->map + op->mapNo    , op->slc, op->toTask, errors);
                    if (laik_tcp_errors_present (errors)) {
                        laik_tcp_errors_push (errors, __func__, 3, "Send operation failed");
                        laik_tcp_errors_abort (errors);
                    }
                }
            } else {
                // It's not our turn to send data, instead we should receive
                // from the current "sender" task.
                for (int i = 0; i < transition->recvCount; i++) {
                    const struct recvTOp* op = transition->recv + i;

                    if (sender == op->fromTask) {
                        laik_tcp_always (output_list);
                        laik_tcp_always (op->mapNo >= 0 && op->mapNo < output_list->count);

                        laik_tcp_backend_receive (data, output_list->map + op->mapNo, op->slc, op->fromTask, errors);
                        if (laik_tcp_errors_present (errors)) {
                            laik_tcp_errors_push (errors, __func__, 4, "Receive operation failed");
                            laik_tcp_errors_abort (errors);
                        }
                    }
                }
            }
        }
    }
}

static void laik_tcp_backend_finalize () {
    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

    // Finalize the MPI subsystem if necessary
    int mpi_is_initialized;
    laik_tcp_backend_push_code (errors, MPI_Initialized (&mpi_is_initialized));
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to determine whether the MPI subsystem is initialized");
        laik_tcp_errors_abort (errors);
    }

    if (mpi_is_initialized) {
        laik_tcp_backend_push_code (errors, MPI_Finalize());
        if (laik_tcp_errors_present (errors)) {
            laik_tcp_errors_push (errors, __func__, 1, "Failed to finalize the MPI subsystem");
            laik_tcp_errors_abort (errors);
        }
    }
}

static void laik_tcp_backend_update_group (Laik_Group* group) {
    laik_tcp_always (group);

    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

    // We are transitioning from an old (parent) group to a new (child) group,
    // so run a few checks to detect programming errors:

    // 1. The new group must actually have a parent group.
    laik_tcp_always (group->parent);

    // 2. The parent group must have its backend_data set up properly.
    laik_tcp_always (group->parent->backend_data);

    // 3. The new (child) group must have no backend_data set yet.
    laik_tcp_always (group->backend_data == NULL);

    // Everything is fine, so allocate memory for our new group's backend_data.
    group->backend_data = g_new0 (MPI_Comm, 1);

    // Run MPI_Comm_split to transition from the old (parent) group's
    // communicator to a new communicator used by the new (child) group.
    // The current task will be part of the new communicator iff its ID is >= 0
    // and it will be ranked according to its ID (this means LAIK ID == MPI ID).
    laik_tcp_backend_push_code (errors, MPI_Comm_split (* ((MPI_Comm*) group->parent->backend_data), group->myid < 0 ? MPI_UNDEFINED : 0, group->myid, group->backend_data));
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to split communicator for updated group");
        laik_tcp_errors_abort (errors);
    }
}

/* Public functions */

Laik_Instance* laik_init_tcp (int* argc, char*** argv) {
    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

    // Prepare our Laik_Backend struct which contains all the function pointers
    static const Laik_Backend backend = {
        .cleanup     = NULL,
        .exec        = laik_tcp_backend_exec,
        .finalize    = laik_tcp_backend_finalize,
        .name        = "TCP Backend",
        .prepare     = NULL,
        .sync        = NULL,
        .updateGroup = laik_tcp_backend_update_group,
    };

    // Determine if the MPI subsystem is already initialized
    int mpi_is_initialized;
    laik_tcp_backend_push_code (errors, MPI_Initialized (&mpi_is_initialized));
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to determine whether the MPI subsystem is initialized");
        laik_tcp_errors_abort (errors);
    }

    // Initialize the MPI subsystem if necessary
    if (!mpi_is_initialized) {
        laik_tcp_backend_push_code (errors, MPI_Init (argc, argv));
        if (laik_tcp_errors_present (errors)) {
            laik_tcp_errors_push (errors, __func__, 1, "Failed to initialize the MPI subsystem");
            laik_tcp_errors_abort (errors);
        }
    }

    // Determine the name of our processor
    char name[MPI_MAX_PROCESSOR_NAME];
    int length;
    laik_tcp_backend_push_code (errors, MPI_Get_processor_name (name, &length));
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 2, "Failed to determine the MPI processor name");
        laik_tcp_errors_abort (errors);
    }

    // Determine our ID in the MPI world
    int myid;
    laik_tcp_backend_push_code (errors, MPI_Comm_rank (MPI_COMM_WORLD, &myid));
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 3, "Failed to determine our rank in the MPI world");
        laik_tcp_errors_abort (errors);
    }

    // Determine the size of the MPI world
    int size;
    laik_tcp_backend_push_code (errors, MPI_Comm_size (MPI_COMM_WORLD, &size));
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 4, "Failed to determine the size of the MPI world");
        laik_tcp_errors_abort (errors);
    }

    // Create a new, private MPI communicator for the instance.
    MPI_Comm* instance_communicator = g_new0 (MPI_Comm, 1);
    laik_tcp_backend_push_code (errors, MPI_Comm_dup (MPI_COMM_WORLD, instance_communicator));
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 5, "Failed to duplicate MPI_COMM_WORLD for instance");
        laik_tcp_errors_abort (errors);
    }

    // Create a new, private MPI communicator for the first group.
    MPI_Comm* group_communicator = g_new0 (MPI_Comm, 1);
    laik_tcp_backend_push_code (errors, MPI_Comm_dup (MPI_COMM_WORLD, group_communicator));
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 6, "Failed to duplicate MPI_COMM_WORLD for group");
        laik_tcp_errors_abort (errors);
    }
 
    // Create the instance.
    Laik_Instance* instance = laik_new_instance (&backend, size, myid, name, instance_communicator, group_communicator);

    // Set up the instance GUID.
    sprintf (instance->guid, "%d", myid);

    // Return the instance
    return instance;
}

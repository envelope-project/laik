#define BENCHMARK "OSU MPI%s Latency Test"
/*
 * Copyright (C) 2002-2019 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University.
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level OMB directory.
 */

/**
 * This is a port of the OSU Latency benchmark to LAIK
 */

#define TSLICE_0_TASK(partitioning) partitioning->saList->slices->tslice[0].task

#include <laik-internal.h>
#include "osu_util_mpi.h"
#include "laik.h"
#include "../fault_tolerance_test.h"

Laik_Instance *inst;
Laik_Group *world;
Laik_Space *space;
Laik_Data *data;
Laik_Checkpoint *spaceCheckpoint = NULL;
int restoreIteration = -1;

void createCheckpoints(int iter, int redundancyCount, int rotationDistance, bool delayCheckpointRelease) {
    if(spaceCheckpoint != NULL && !delayCheckpointRelease) {
        TPRINTF("Freeing previous checkpoint from iteration %i\n", restoreIteration);
        laik_free(spaceCheckpoint->data);
    }
    TRACE_EVENT_S("CHECKPOINT-PRE-NEW", "");
    TPRINTF("Creating checkpoint of data\n");
    Laik_Checkpoint* newCheckpoint = laik_checkpoint_create(data, NULL, redundancyCount,
                                                            rotationDistance, world, LAIK_RO_None);
    TRACE_EVENT_S("CHECKPOINT-POST-NEW", "");
    TPRINTF("Checkpoint successful at iteration %i\n", iter);

    if(spaceCheckpoint != NULL && delayCheckpointRelease) {
        TPRINTF("Freeing previous checkpoint from iteration %i\n", restoreIteration);
        laik_free(spaceCheckpoint->data);
    }

    spaceCheckpoint = newCheckpoint;
    restoreIteration = iter;
}

void restoreCheckpoints() {
    TPRINTF("Restoring from checkpoint (checkpoint iteration %i)\n", restoreIteration);
//    laik_partitioning_migrate(spaceCheckpoint->data->activePartitioning, world);
    laik_checkpoint_restore(spaceCheckpoint, data);
    TPRINTF("Restore successful\n");
}


void createPartitionings(Laik_Partitioner *(singlePartitioners[]),
                         Laik_Partitioning *(singlePartitionings[])) {
    for (int task = 0; task < world->size; ++task) {
        singlePartitionings[task] = laik_new_partitioning(singlePartitioners[task], world, space, NULL);
    }
}


int main (int argc, char *argv[])
{
    int myid, numprocs;
    size_t size, i;
//    char *s_buf, *r_buf;
    double t_start = 0.0, t_end = 0.0;
    int po_ret = 0;
    options.bench = PT2PT;
    options.subtype = LAT;

    set_header(HEADER);
    set_benchmark_name("osu_latency");

    FaultToleranceOptions faultToleranceOptions = FaultToleranceOptionsDefault;

    inst = laik_init(&argc, &argv);
    world = laik_world(inst);
    numprocs = laik_size(world);
    myid = laik_myid(world);

    laik_error_handler_set(inst, laik_failure_default_error_handler);

    po_ret = process_options(argc, argv, myid, &faultToleranceOptions);

    if (PO_OKAY == po_ret && NONE != options.accel) {
        if (init_accel()) {
            fprintf(stderr, "Error initializing device\n");
            exit(EXIT_FAILURE);
        }
    }

    TRACE_INIT(myid);
    TRACE_EVENT_START("INIT", "");

    if (0 == myid) {
        switch (po_ret) {
            case PO_CUDA_NOT_AVAIL:
                fprintf(stderr, "CUDA support not enabled.  Please recompile "
                                "benchmark with CUDA support.\n");
                break;
            case PO_OPENACC_NOT_AVAIL:
                fprintf(stderr, "OPENACC support not enabled.  Please "
                                "recompile benchmark with OPENACC support.\n");
                break;
            case PO_BAD_USAGE:
                print_bad_usage_message(myid);
                break;
            case PO_HELP_MESSAGE:
                print_help_message(myid);
                break;
            case PO_VERSION_MESSAGE:
                print_version_message(myid);
                laik_finalize(inst);
                exit(EXIT_SUCCESS);
            case PO_OKAY:
                break;
        }
    }

    switch (po_ret) {
        case PO_CUDA_NOT_AVAIL:
        case PO_OPENACC_NOT_AVAIL:
        case PO_BAD_USAGE:
            laik_finalize(inst);
            exit(EXIT_FAILURE);
        case PO_HELP_MESSAGE:
        case PO_VERSION_MESSAGE:
            laik_finalize(inst);
            exit(EXIT_SUCCESS);
        case PO_OKAY:
            break;
    }

    if(myid == 0) {
        printf("Running OSU Latency Ring benchmark on %i processes\n", numprocs);
    }
//    if(numprocs != 2) {
//        if(myid == 0) {
//            fprintf(stderr, "This test requires exactly two processes\n");
//        }
//
//        laik_finalize(inst);
//        exit(EXIT_FAILURE);
//    }

//    Laik_Space* space = laik_new_space_1d(inst, options.max_message_size);
//    Laik_Data* data = laik_new_data(space, laik_Char);

//    Laik_Partitioner* copyPartitioner = laik_new_copy_partitioner(1, 1);
//    Laik_Partitioning* t1Partitioning = laik_new_partitioning(laik_Master, world, space, NULL);
//    Laik_Partitioning* t2Partitioning = laik_new_partitioning(laik_Master, world, space, NULL);

    //Modify second partitioning, such that it resides on task 1 instead of task 0
//    t2Partitioning->saList->slices->tslice[0].task++;

//    uint64_t dCount;
//    laik_map_def1(data, (void**)&s_buf, &dCount);

    print_header(myid, LAT);


    size = options.min_message_size;
    //Laik doesn't like a space with size <= 0
    if(size <= 0) {
        if(laik_myid(laik_world(inst))) {
            printf("Start size %zu <= 0, setting to 1.\n", size);
        }

        size = 1;
    }

    Laik_Partitioner *(singlePartitioners[world->size]);
    Laik_Partitioning *(singlePartitionings[world->size]);
    for (int task = 0; task < world->size; ++task) {
        singlePartitioners[task] = laik_new_single_partitioner(task);
    }

    int nodeStatuses[world->size];

    TRACE_EVENT_END("INIT", "");
    /* Latency test */
    for(/* Initialized above */; size <= options.max_message_size; size = (size ? size * 2 : 1)) {
        space = laik_new_space_1d(inst, size);
        data = laik_new_data(space, laik_Char);

        createPartitionings(singlePartitioners, singlePartitionings);
//        Laik_Partitioning* newT1Partitioning = laik_new_partitioning(singlePartitioner, world, laik_data_get_space(data), NULL);
//        singlePartitioner->data = (void*)(((Laik_SinglePartitionerData)singlePartitioner->data) + 1);
//        Laik_Partitioning* newT2Partitioning = laik_new_partitioning(singlePartitioner, world, laik_data_get_space(data), NULL);
//
//        //Modify second partitioning, such that it resides on task 1 instead of task 0
//        TSLICE_0_TASK(newT2Partitioning)++;
//
//        assert(newT1Partitioning->saList->slices->tslice[0].task == 0);
//        assert(TSLICE_0_TASK(newT2Partitioning) == 1);

        laik_switchto_partitioning(data, singlePartitionings[0], LAIK_DF_None, LAIK_RO_None);
//        laik_switchto_partitioning(data, newT2Partitioning, LAIK_DF_None, LAIK_RO_None);

        char* base;
        uint64_t count;
        assert(laik_my_mapcount(laik_data_get_partitioning(data)) == 1);
        laik_get_map_1d(data,0, (void**)&base, &count);

//        t1Partitioning = newT1Partitioning;
//        t2Partitioning = newT2Partitioning;

        if(size > LARGE_MESSAGE_SIZE) {
            options.iterations = options.iterations_large;
            options.skip = options.skip_large;
        }

//        MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

        for(i = 0; i < options.iterations + options.skip; i++) {
            laik_set_iteration(inst, i);
            if(i % 10000 == 0) {
                TRACE_EVENT_S("ITER", "");
            }
            if (isFaultToleranceActive(&faultToleranceOptions) && i % faultToleranceOptions.failureCheckFrequency == 0) {
                TPRINTF("Attempting to determine global status.\n");
                TRACE_EVENT_START("FAILURE-CHECK", "");
                Laik_Group *checkGroup = world;
                int numFailed = laik_failure_check_nodes(inst, checkGroup, nodeStatuses);
                TRACE_EVENT_END("FAILURE-CHECK", "");
                if (numFailed == 0) {
                    TPRINTF("Could not detect a failed node.\n");
                } else {
                    TRACE_EVENT_S("FAILURE-DETECT", "");
                    // Don't allow any failures while recovery
                    laik_log(LAIK_LL_Info, "Deactivating error handler!");
                    laik_error_handler_set(inst, NULL);

                    laik_failure_eliminate_nodes(inst, numFailed, nodeStatuses);

                    // Re-fetch the world
                    world = laik_world_fault_tolerant(inst);

                    TPRINTF("Attempting to restore with new world size %i\n", world->size);

                    TRACE_EVENT_START("RESTORE", "");
                    createPartitionings(singlePartitioners, singlePartitionings);

                    TPRINTF("Switching to new partitionings\n");
                    laik_switchto_partitioning(data, singlePartitionings[0], LAIK_DF_None, LAIK_RO_None);

                    if (!faultToleranceOptions.skipCheckpointRecovery) {
                        TPRINTF("Removing failed slices from checkpoints\n");
                        if (!laik_checkpoint_remove_failed_slices(spaceCheckpoint, checkGroup, nodeStatuses)) {
                            TPRINTF("A checkpoint no longer covers its entire space, some data was irreversibly lost. Abort.\n");
                            abort();
                        }

                        restoreCheckpoints();
                        i = restoreIteration;
                    } else {
                        laik_log(LAIK_LL_Info, "Skipping checkpoint restore.");
                    }

                    TRACE_EVENT_END("RESTORE", "");
                    TPRINTF("Restore complete, cleared errors.\n");

//                TPRINTF("Special: Switching to all partitioning.\n");
//                Laik_Partitioning* pMaster = laik_new_partitioning(laik_Master, world, space, NULL);
//
//                laik_switchto_partitioning(data1, pMaster, LAIK_DF_Preserve, LAIK_RO_None);
//                laik_switchto_partitioning(data2, pMaster, LAIK_DF_Preserve, LAIK_RO_None);
//
//                TPRINTF("Special: Switched to all partitioning.\n");

                    // Restored normal state, errors are allowed now
                    laik_log(LAIK_LL_Info, "Reactivating error handler!");
                    laik_error_handler_set(inst, laik_failure_default_error_handler);
                }
            }

            // At every checkpointFrequency iterations, do a checkpoint
            if (faultToleranceOptions.checkpointFrequency > 0 && i % faultToleranceOptions.checkpointFrequency == 0) {
                TRACE_EVENT_START("CHECKPOINT", "");
                createCheckpoints(i, faultToleranceOptions.redundancyCount, faultToleranceOptions.rotationDistance, faultToleranceOptions.delayCheckpointRelease);
                TRACE_EVENT_END("CHECKPOINT", "");
            }


            if (i == options.skip) {
                t_start = laik_wtime();
            }
            int nextId = (int)i % world->size;
//            printf("Switch to single (task id %i)\n", TSLICE_0_TASK(singlePartitionings[nextId]));
            laik_switchto_partitioning(data, singlePartitionings[nextId], LAIK_DF_Preserve, LAIK_RO_None);
            assert(laik_my_slicecount(laik_data_get_partitioning(data)) == 1);
            laik_get_map_1d(data, 0, (void**)&base, &count);

            // Execute any pre planned failures
            exitIfFailureIteration(i, &faultToleranceOptions, inst);
        }
        t_end = laik_wtime();

        for (int task = 0; task < world->size; ++task) {
            laik_free_partitioning(singlePartitionings[task]);
        }
        laik_free(data);
        laik_free_space(space);

        if(myid == 0) {
            double latency = (t_end - t_start) * 1e6 / (2.0 * options.iterations);

            fprintf(stdout, "%-*zu%*.*f\n", 10, size, FIELD_WIDTH,
                    FLOAT_PRECISION, latency);
            fflush(stdout);
        }
    }

    TRACE_EVENT_START("FINALIZE", "");
//    laik_free(data);
    laik_finalize(inst);

    if (NONE != options.accel) {
        if (cleanup_accel()) {
            fprintf(stderr, "Error cleaning up device\n");
            exit(EXIT_FAILURE);
        }
    }

    TRACE_EVENT_END("FINALIZE", "");
    return EXIT_SUCCESS;
}

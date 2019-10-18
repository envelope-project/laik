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

int main (int argc, char *argv[])
{
    laik_set_loglevel(LAIK_LL_Warning);
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

    Laik_Instance* inst = laik_init(&argc, &argv);
    Laik_Group *world = laik_world(inst);
    numprocs = laik_size(world);
    myid = laik_myid(world);

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

    TRACE_EVENT_END("INIT", "");
    /* Latency test */
    for(/* Initialized above */; size <= options.max_message_size; size = (size ? size * 2 : 1)) {
        Laik_Space* space = laik_new_space_1d(inst, size);
        Laik_Data* data = laik_new_data(space, laik_Char);

        for (int task = 0; task < world->size; ++task) {
            singlePartitionings[task] = laik_new_partitioning(singlePartitioners[task], world, space, NULL);
        }
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
        laik_map_def1(data, (void**)&base, &count);

//        t1Partitioning = newT1Partitioning;
//        t2Partitioning = newT2Partitioning;

        if(size > LARGE_MESSAGE_SIZE) {
            options.iterations = options.iterations_large;
            options.skip = options.skip_large;
        }

//        MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

        for(i = 0; i < options.iterations + options.skip; i++) {
            if(i % 10000 == 0) {
                TRACE_EVENT_S("ITER", "");
            }
            if (i == options.skip) {
                t_start = laik_wtime();
            }
            int nextId = (int)i % world->size;
//            printf("Switch to single (task id %i)\n", TSLICE_0_TASK(singlePartitionings[nextId]));
            laik_switchto_partitioning(data, singlePartitionings[nextId], LAIK_DF_Preserve, LAIK_RO_None);
            laik_map_def1(data, (void**)&base, &count);

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

/*
 * Copyright (C) 2002-2019 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University.
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level directory.
 */

#include "osu_util_mpi.h"

MPI_Request request[MAX_REQ_NUM];
MPI_Status  reqstat[MAX_REQ_NUM];
MPI_Request send_request[MAX_REQ_NUM];
MPI_Request recv_request[MAX_REQ_NUM];

MPI_Aint disp_remote;
MPI_Aint disp_local;

/* A is the A in DAXPY for the Compute Kernel */
#define A 2.0
#define DEBUG 0
/*
 * We are using a 2-D matrix to perform dummy
 * computation in non-blocking collective benchmarks
 */
#define DIM 25
static float **a, *x, *y;

#ifdef _ENABLE_CUDA_
CUcontext cuContext;
#endif

char const *win_info[20] = {
    "MPI_Win_create",
#if MPI_VERSION >= 3
    "MPI_Win_allocate",
    "MPI_Win_create_dynamic",
#endif
};

char const *sync_info[20] = {
    "MPI_Win_lock/unlock",
    "MPI_Win_post/start/complete/wait",
    "MPI_Win_fence",
#if MPI_VERSION >= 3
    "MPI_Win_flush",
    "MPI_Win_flush_local",
    "MPI_Win_lock_all/unlock_all",
#endif
};

#ifdef _ENABLE_CUDA_KERNEL_
/* Using new stream for kernels on gpu */
static cudaStream_t stream;

static int is_alloc = 0;

/* Arrays on device for dummy compute */
static float *d_x, *d_y;
#endif

void set_device_memory (void * ptr, int data, size_t size)
{
#ifdef _ENABLE_OPENACC_
    size_t i;
    char * p = (char *)ptr;
#endif

    switch (options.accel) {
#ifdef _ENABLE_CUDA_
        case CUDA:
            CUDA_CHECK(cudaMemset(ptr, data, size));
            break;
#endif
#ifdef _ENABLE_OPENACC_
        case OPENACC:
#pragma acc parallel copyin(size) deviceptr(p)
            for(i = 0; i < size; i++) {
                p[i] = data;
            }
            break;
#endif
        default:
            break;
    }
}

int free_device_buffer (void * buf)
{
    switch (options.accel) {
#ifdef _ENABLE_CUDA_
        case CUDA:
            CUDA_CHECK(cudaFree(buf));
            break;
#endif
#ifdef _ENABLE_OPENACC_
        case OPENACC:
            acc_free(buf);
            break;
#endif
        default:
            /* unknown device */
            return 1;
    }

    buf = NULL;
    return 0;
}

void *align_buffer (void * ptr, unsigned long align_size)
{
    unsigned long buf = (((unsigned long)ptr + (align_size - 1)) / align_size * align_size);
    return (void *) buf;
}


void usage_one_sided (char const * name)
{
    if (accel_enabled) {
        fprintf(stdout, "Usage: %s [options] [SRC DST]\n\n", name);
        fprintf(stdout, "SRC and DST are buffer types for the source and destination\n");
        fprintf(stdout, "SRC and DST may be `D' or `H' which specifies whether\n"
                        "the buffer is allocated on the accelerator device or host\n"
                        "memory for each mpi rank\n\n");
    } else {
        fprintf(stdout, "Usage: %s [options]\n", name);
    }

    fprintf(stdout, "Options:\n");

    if (accel_enabled) {
        fprintf(stdout, "  -d --accelerator <type>       accelerator device buffers can be of <type> "
                   "`cuda' or `openacc'\n");
    }
    fprintf(stdout, "\n");

#if MPI_VERSION >= 3
    fprintf(stdout, "  -w --win-option <win_option>\n");
    fprintf(stdout, "            <win_option> can be any of the follows:\n");
    fprintf(stdout, "            create            use MPI_Win_create to create an MPI Window object\n");
    if (accel_enabled) {
        fprintf(stdout, "            allocate          use MPI_Win_allocate to create an MPI Window object (not valid when using device memory)\n");
    } else {
        fprintf(stdout, "            allocate          use MPI_Win_allocate to create an MPI Window object\n");
    }
    fprintf(stdout, "            dynamic           use MPI_Win_create_dynamic to create an MPI Window object\n");
    fprintf(stdout, "\n");
#endif

    fprintf(stdout, "  -s, --sync-option <sync_option>\n");
    fprintf(stdout, "            <sync_option> can be any of the follows:\n");
    fprintf(stdout, "            pscw              use Post/Start/Complete/Wait synchronization calls \n");
    fprintf(stdout, "            fence             use MPI_Win_fence synchronization call\n");
    if (options.synctype == ALL_SYNC) {
        fprintf(stdout, "            lock              use MPI_Win_lock/unlock synchronizations calls\n");
#if MPI_VERSION >= 3
        fprintf(stdout, "            flush             use MPI_Win_flush synchronization call\n");
        fprintf(stdout, "            flush_local       use MPI_Win_flush_local synchronization call\n");
        fprintf(stdout, "            lock_all          use MPI_Win_lock_all/unlock_all synchronization calls\n");
#endif
    }
    fprintf(stdout, "\n");
    if (options.show_size) {
        fprintf(stdout, "  -m, --message-size          [MIN:]MAX  set the minimum and/or the maximum message size to MIN and/or MAX\n");
        fprintf(stdout, "                              bytes respectively. Examples:\n");
        fprintf(stdout, "                              -m 128      // min = default, max = 128\n");
        fprintf(stdout, "                              -m 2:128    // min = 2, max = 128\n");
        fprintf(stdout, "                              -m 2:       // min = 2, max = default\n");
        fprintf(stdout, "  -M, --mem-limit SIZE        set per process maximum memory consumption to SIZE bytes\n");
        fprintf(stdout, "                              (default %d)\n", MAX_MEM_LIMIT);
    }
    fprintf(stdout, "  -x, --warmup ITER           number of warmup iterations to skip before timing"
                   "(default 100)\n");
    fprintf(stdout, "  -i, --iterations ITER       number of iterations for timing (default 10000)\n");

    fprintf(stdout, "  -h, --help                  print this help message\n");
    fflush(stdout);
}

int process_one_sided_options (int opt, char *arg)
{
    switch(opt) {
        case 'w':
#if MPI_VERSION >= 3
            if (0 == strcasecmp(arg, "create")) {
                options.win = WIN_CREATE;
            } else if (0 == strcasecmp(arg, "allocate")) {
                options.win = WIN_ALLOCATE;
            } else if (0 == strcasecmp(arg, "dynamic")) {
                options.win = WIN_DYNAMIC;
            } else
#endif
            {
                return PO_BAD_USAGE;
            }
            break;
        case 's':
            if (0 == strcasecmp(arg, "pscw")) {
                options.sync = PSCW;
            } else if (0 == strcasecmp(arg, "fence")) {
                options.sync = FENCE;
            } else if (options.synctype== ALL_SYNC) {
                if (0 == strcasecmp(arg, "lock")) {
                    options.sync = LOCK;
                }
#if MPI_VERSION >= 3
                else if (0 == strcasecmp(arg, "flush")) {
                    options.sync = FLUSH;
                } else if (0 == strcasecmp(arg, "flush_local")) {
                    options.sync = FLUSH_LOCAL;
                } else if (0 == strcasecmp(arg, "lock_all")) {
                    options.sync = LOCK_ALL;
                }
#endif
                else {
                    return PO_BAD_USAGE;
                }
            } else {
                return PO_BAD_USAGE;
            }
            break;
        default:
            return PO_BAD_USAGE;
    }

    return PO_OKAY;
}

void usage_mbw_mr()
{
    if (accel_enabled) {
        fprintf(stdout, "Usage: osu_mbw_mr [options] [SRC DST]\n\n");
        fprintf(stdout, "SRC and DST are buffer types for the source and destination\n");
        fprintf(stdout, "SRC and DST may be `D', `H', or 'M' which specifies whether\n"
                        "the buffer is allocated on the accelerator device memory, host\n"
                        "memory or using CUDA Unified memory respectively for each mpi rank\n\n");
    } else {
        fprintf(stdout, "Usage: osu_mbw_mr [options]\n");
    }

    fprintf(stdout, "Options:\n");
    fprintf(stdout, "  -R=<0,1>, --print-rate         Print uni-directional message rate (default 1)\n");
    fprintf(stdout, "  -p=<pairs>, --num-pairs        Number of pairs involved (default np / 2)\n");
    fprintf(stdout, "  -W=<window>, --window-size     Number of messages sent before acknowledgement (default 64)\n");
    fprintf(stdout, "                                 [cannot be used with -v]\n");
    fprintf(stdout, "  -V, --vary-window              Vary the window size (default no)\n");
    fprintf(stdout, "                                 [cannot be used with -W]\n");
    if (options.show_size) {
        fprintf(stdout, "  -m, --message-size          [MIN:]MAX  set the minimum and/or the maximum message size to MIN and/or MAX\n");
        fprintf(stdout, "                              bytes respectively. Examples:\n");
        fprintf(stdout, "                              -m 128      // min = default, max = 128\n");
        fprintf(stdout, "                              -m 2:128    // min = 2, max = 128\n");
        fprintf(stdout, "                              -m 2:       // min = 2, max = default\n");
        fprintf(stdout, "  -M, --mem-limit SIZE        set per process maximum memory consumption to SIZE bytes\n");
        fprintf(stdout, "                              (default %d)\n", MAX_MEM_LIMIT);
    }
    if (accel_enabled) {
        fprintf(stdout, "  -d, --accelerator  TYPE     use accelerator device buffers, which can be of TYPE `cuda', \n");
        fprintf(stdout, "                              `managed' or `openacc' (uses standard host buffers if not specified)\n");
    }
    fprintf(stdout, "  -h, --help                     Print this help\n");
    fprintf(stdout, "\n");
    fprintf(stdout, "  Note: This benchmark relies on block ordering of the ranks.  Please see\n");
    fprintf(stdout, "        the README for more information.\n");
    fflush(stdout);
}

void print_bad_usage_message (int rank)
{
    if (rank) {
        return;
    }

    if (bad_usage.optarg) {
        fprintf(stderr, "%s [-%c %s]\n\n", bad_usage.message,
                (char)bad_usage.opt, bad_usage.optarg);
    } else {
        fprintf(stderr, "%s [-%c]\n\n", bad_usage.message,
                (char)bad_usage.opt);
    }
    fflush(stderr);

    if (options.bench != ONE_SIDED) {
        print_help_message(rank);
    }
}

void print_help_message (int rank)
{
    if (rank) {
        return;
    }

    if (accel_enabled && (options.bench == PT2PT)) {
        fprintf(stdout, "Usage: %s [options] [SRC DST]\n\n", benchmark_name);
        fprintf(stdout, "SRC and DST are buffer types for the source and destination\n");
        fprintf(stdout, "SRC and DST may be `D', `H', or 'M' which specifies whether\n"
                        "the buffer is allocated on the accelerator device memory, host\n"
                        "memory or using CUDA Unified memory respectively for each mpi rank\n\n");
    } else {
        fprintf(stdout, "Usage: %s [options]\n", benchmark_name);
        fprintf(stdout, "Options:\n");
    }

    if (accel_enabled && (options.subtype != LAT_MT)) {
        fprintf(stdout, "  -d, --accelerator  TYPE     use accelerator device buffers, which can be of TYPE `cuda', \n");
        fprintf(stdout, "                              `managed' or `openacc' (uses standard host buffers if not specified)\n");
    }

    if (options.show_size) {
        fprintf(stdout, "  -m, --message-size          [MIN:]MAX  set the minimum and/or the maximum message size to MIN and/or MAX\n");
        fprintf(stdout, "                              bytes respectively. Examples:\n");
        fprintf(stdout, "                              -m 128      // min = default, max = 128\n");
        fprintf(stdout, "                              -m 2:128    // min = 2, max = 128\n");
        fprintf(stdout, "                              -m 2:       // min = 2, max = default\n");
        fprintf(stdout, "  -M, --mem-limit SIZE        set per process maximum memory consumption to SIZE bytes\n");
        fprintf(stdout, "                              (default %d)\n", MAX_MEM_LIMIT);
    }

    fprintf(stdout, "  -i, --iterations ITER       set iterations per message size to ITER (default 1000 for small\n");
    fprintf(stdout, "                              messages, 100 for large messages)\n");
    fprintf(stdout, "  -x, --warmup ITER           set number of warmup iterations to skip before timing (default 200)\n");

    if (options.subtype == BW) {
        fprintf(stdout, "  -W, --window-size SIZE      set number of messages to send before synchronization (default 64)\n");
    }

    if (options.bench == COLLECTIVE) {
        fprintf(stdout, "  -f, --full                  print full format listing (MIN/MAX latency and ITERATIONS\n");
        fprintf(stdout, "                              displayed in addition to AVERAGE latency)\n");

        if (options.subtype == NBC) {
            fprintf(stdout, "  -t, --num_test_calls CALLS  set the number of MPI_Test() calls during the dummy computation, \n");
            fprintf(stdout, "                              set CALLS to 100, 1000, or any number > 0.\n");
        }

        if (CUDA_KERNEL_ENABLED) {
            fprintf(stdout, "  -r, --cuda-target TARGET    set the compute target for dummy computation\n");
            fprintf(stdout, "                              set TARGET to cpu (default) to execute \n");
            fprintf(stdout, "                              on CPU only, set to gpu for executing kernel \n");
            fprintf(stdout, "                              on the GPU only, and set to both for compute on both.\n");

            fprintf(stdout, "  -a, --array-size SIZE       set the size of arrays to be allocated on device (GPU) \n");
            fprintf(stdout, "                              for dummy compute on device (GPU) (default 32) \n");
        }
    }
    if (LAT_MT == options.subtype) {
        fprintf(stdout, "  -t, --num_threads           SEND:[RECV]  set the sender and receiver number of threads \n");
        fprintf(stdout, "                              min: %d default: (receiver threads: %d sender threads: 1), max: %d.\n", MIN_NUM_THREADS, DEF_NUM_THREADS, MAX_NUM_THREADS);
        fprintf(stdout, "                              Examples: \n");
        fprintf(stdout, "                              -t 4        // receiver threads = 4 and sender threads = 1\n");
        fprintf(stdout, "                              -t 4:6      // sender threads = 4 and receiver threads = 6\n");
        fprintf(stdout, "                              -t 2:       // not defined\n");
        fprintf(stdout, "  -M, --mem-limit SIZE        set per process maximum memory consumption to SIZE bytes\n");
    }

    fprintf(stdout, "  -h, --help                  print this help\n");
    fprintf(stdout, "  -v, --version               print version info\n");
    fprintf(stdout, "\n");
    fflush(stdout);
}

void print_help_message_get_acc_lat (int rank)
{
    if (rank) {
        return;
    }

    if (bad_usage.optarg) {
        fprintf(stderr, "%s [-%c %s]\n\n", bad_usage.message,
                (char)bad_usage.opt, bad_usage.optarg);
    } else {
        fprintf(stderr, "%s [-%c]\n\n", bad_usage.message,
                (char)bad_usage.opt);
    }
    fflush(stderr);

    fprintf(stdout, "Usage: ./osu_get_acc_latency -w <win_option>  -s < sync_option> [-x ITER] [-i ITER]\n");
    if (options.show_size) {
        fprintf(stdout, "  -m, --message-size          [MIN:]MAX  set the minimum and/or the maximum message size to MIN and/or MAX\n");
        fprintf(stdout, "                              bytes respectively. Examples:\n");
        fprintf(stdout, "                              -m 128      // min = default, max = 128\n");
        fprintf(stdout, "                              -m 2:128    // min = 2, max = 128\n");
        fprintf(stdout, "                              -m 2:       // min = 2, max = default\n");
        fprintf(stdout, "  -M, --mem-limit SIZE        set per process maximum memory consumption to SIZE bytes\n");
        fprintf(stdout, "                              (default %d)\n", MAX_MEM_LIMIT);
    }

    fprintf(stdout, "  -x ITER       number of warmup iterations to skip before timing"
            "(default 100)\n");
    fprintf(stdout, "  -i ITER       number of iterations for timing (default 10000)\n");
    fprintf(stdout, "\n");
    fprintf(stdout, "win_option:\n");
    fprintf(stdout, "  create            use MPI_Win_create to create an MPI Window object\n");
    fprintf(stdout, "  allocate          use MPI_Win_allocate to create an MPI Window object\n");
    fprintf(stdout, "  dynamic           use MPI_Win_create_dynamic to create an MPI Window object\n");
    fprintf(stdout, "\n");

    fprintf(stdout, "sync_option:\n");
    fprintf(stdout, "  lock              use MPI_Win_lock/unlock synchronizations calls\n");
    fprintf(stdout, "  flush             use MPI_Win_flush synchronization call\n");
    fprintf(stdout, "  flush_local       use MPI_Win_flush_local synchronization call\n");
    fprintf(stdout, "  lock_all          use MPI_Win_lock_all/unlock_all synchronization calls\n");
    fprintf(stdout, "  pscw              use Post/Start/Complete/Wait synchronization calls \n");
    fprintf(stdout, "  fence             use MPI_Win_fence synchronization call\n");
    fprintf(stdout, "\n");

    fflush(stdout);
}

void print_header_one_sided (int rank, enum WINDOW win, enum SYNC sync)
{
    if(rank == 0) {
        switch (options.accel) {
            case CUDA:
                printf(benchmark_header, "-CUDA");
                break;
            case OPENACC:
                printf(benchmark_header, "-OPENACC");
                break;
            default:
                printf(benchmark_header, "");
                break;
        }
        fprintf(stdout, "# Window creation: %s\n",
                win_info[win]);
        fprintf(stdout, "# Synchronization: %s\n",
                sync_info[sync]);

        switch (options.accel) {
            case CUDA:
            case OPENACC:
                fprintf(stdout, "# Rank 0 Memory on %s and Rank 1 Memory on %s\n",
                       'M' == options.src ? "MANAGED (M)" : ('D' == options.src ? "DEVICE (D)" : "HOST (H)"),
                       'M' == options.dst ? "MANAGED (M)" : ('D' == options.dst ? "DEVICE (D)" : "HOST (H)"));
            default:
                if (options.subtype == BW) {
                    fprintf(stdout, "%-*s%*s\n", 10, "# Size", FIELD_WIDTH, "Bandwidth (MB/s)");
                } else {
                    fprintf(stdout, "%-*s%*s\n", 10, "# Size", FIELD_WIDTH, "Latency (us)");
                }
                fflush(stdout);
        }
    }
}

void print_version_message (int rank)
{
    if (rank) {
        return;
    }

    switch (options.accel) {
        case CUDA:
            printf(benchmark_header, "-CUDA");
            break;
        case OPENACC:
            printf(benchmark_header, "-OPENACC");
            break;
        case MANAGED:
            printf(benchmark_header, "-CUDA MANAGED");
            break;
        default:
            printf(benchmark_header, "");
            break;
    }

    fflush(stdout);
}

void print_preamble_nbc (int rank)
{
    if (rank) {
        return;
    }

    fprintf(stdout, "\n");

    switch (options.accel) {
        case CUDA:
            printf(benchmark_header, "-CUDA");
            break;
        case OPENACC:
            printf(benchmark_header, "-OPENACC");
            break;
        case MANAGED:
            printf(benchmark_header, "-MANAGED");
            break;
        default:
            printf(benchmark_header, "");
            break;
    }

    fprintf(stdout, "# Overall = Coll. Init + Compute + MPI_Test + MPI_Wait\n\n");

    if (options.show_size) {
        fprintf(stdout, "%-*s", 10, "# Size");
        fprintf(stdout, "%*s", FIELD_WIDTH, "Overall(us)");
    } else {
        fprintf(stdout, "%*s", FIELD_WIDTH, "Overall(us)");
    }

    if (options.show_full) {
        fprintf(stdout, "%*s", FIELD_WIDTH, "Compute(us)");
        fprintf(stdout, "%*s", FIELD_WIDTH, "Coll. Init(us)");
        fprintf(stdout, "%*s", FIELD_WIDTH, "MPI_Test(us)");
        fprintf(stdout, "%*s", FIELD_WIDTH, "MPI_Wait(us)");
        fprintf(stdout, "%*s", FIELD_WIDTH, "Pure Comm.(us)");
        fprintf(stdout, "%*s\n", FIELD_WIDTH, "Overlap(%)");

    } else {
        fprintf(stdout, "%*s", FIELD_WIDTH, "Compute(us)");
        fprintf(stdout, "%*s", FIELD_WIDTH, "Pure Comm.(us)");
        fprintf(stdout, "%*s\n", FIELD_WIDTH, "Overlap(%)");
    }

    fflush(stdout);
}

void display_nbc_params()
{
    if (options.show_full) {
        fprintf(stdout, "%*s", FIELD_WIDTH, "Compute(us)");
        fprintf(stdout, "%*s", FIELD_WIDTH, "Coll. Init(us)");
        fprintf(stdout, "%*s", FIELD_WIDTH, "MPI_Test(us)");
        fprintf(stdout, "%*s", FIELD_WIDTH, "MPI_Wait(us)");
        fprintf(stdout, "%*s", FIELD_WIDTH, "Pure Comm.(us)");
        fprintf(stdout, "%*s\n", FIELD_WIDTH, "Overlap(%)");

    } else {
        fprintf(stdout, "%*s", FIELD_WIDTH, "Compute(us)");
        fprintf(stdout, "%*s", FIELD_WIDTH, "Pure Comm.(us)");
        fprintf(stdout, "%*s\n", FIELD_WIDTH, "Overlap(%)");
    }
}

void print_preamble (int rank)
{
    if (rank) {
        return;
    }

    fprintf(stdout, "\n");

    switch (options.accel) {
        case CUDA:
            printf(benchmark_header, "-CUDA");
            break;
        case OPENACC:
            printf(benchmark_header, "-OPENACC");
            break;
        default:
            printf(benchmark_header, "");
            break;
    }

    if (options.show_size) {
        fprintf(stdout, "%-*s", 10, "# Size");
        fprintf(stdout, "%*s", FIELD_WIDTH, "Avg Latency(us)");
    } else {
        fprintf(stdout, "# Avg Latency(us)");
    }

    if (options.show_full) {
        fprintf(stdout, "%*s", FIELD_WIDTH, "Min Latency(us)");
        fprintf(stdout, "%*s", FIELD_WIDTH, "Max Latency(us)");
        fprintf(stdout, "%*s\n", 12, "Iterations");
    } else {
        fprintf(stdout, "\n");
    }

    fflush(stdout);
}

void calculate_and_print_stats(int rank, int size, int numprocs,
                          double timer, double latency,
                          double test_time, double cpu_time,
                          double wait_time, double init_time)
{
    double test_total   = (test_time * 1e6) / options.iterations;
    double tcomp_total  = (cpu_time * 1e6) / options.iterations;
    double overall_time = (timer * 1e6) / options.iterations;
    double wait_total   = (wait_time * 1e6) / options.iterations;
    double init_total   = (init_time * 1e6) / options.iterations;
    double comm_time   = latency;

    if(rank != 0) {
        MPI_CHECK(MPI_Reduce(&test_total, &test_total, 1, MPI_DOUBLE, MPI_SUM, 0,
                   MPI_COMM_WORLD));
        MPI_CHECK(MPI_Reduce(&comm_time, &comm_time, 1, MPI_DOUBLE, MPI_SUM, 0,
                   MPI_COMM_WORLD));
        MPI_CHECK(MPI_Reduce(&overall_time, &overall_time, 1, MPI_DOUBLE, MPI_SUM, 0,
                   MPI_COMM_WORLD));
        MPI_CHECK(MPI_Reduce(&tcomp_total, &tcomp_total, 1, MPI_DOUBLE, MPI_SUM, 0,
                   MPI_COMM_WORLD));
        MPI_CHECK(MPI_Reduce(&wait_total, &wait_total, 1, MPI_DOUBLE, MPI_SUM, 0,
                   MPI_COMM_WORLD));
        MPI_CHECK(MPI_Reduce(&init_total, &init_total, 1, MPI_DOUBLE, MPI_SUM, 0,
                   MPI_COMM_WORLD));
    } else {
        MPI_CHECK(MPI_Reduce(MPI_IN_PLACE, &test_total, 1, MPI_DOUBLE, MPI_SUM, 0,
                   MPI_COMM_WORLD));
        MPI_CHECK(MPI_Reduce(MPI_IN_PLACE, &comm_time, 1, MPI_DOUBLE, MPI_SUM, 0,
                   MPI_COMM_WORLD));
        MPI_CHECK(MPI_Reduce(MPI_IN_PLACE, &overall_time, 1, MPI_DOUBLE, MPI_SUM, 0,
                   MPI_COMM_WORLD));
        MPI_CHECK(MPI_Reduce(MPI_IN_PLACE, &tcomp_total, 1, MPI_DOUBLE, MPI_SUM, 0,
                   MPI_COMM_WORLD));
        MPI_CHECK(MPI_Reduce(MPI_IN_PLACE, &wait_total, 1, MPI_DOUBLE, MPI_SUM, 0,
                   MPI_COMM_WORLD));
        MPI_CHECK(MPI_Reduce(MPI_IN_PLACE, &init_total, 1, MPI_DOUBLE, MPI_SUM, 0,
                   MPI_COMM_WORLD));
    }

    MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

    /* Overall Time (Overlapped) */
    overall_time = overall_time/numprocs;
    /* Computation Time */
    tcomp_total = tcomp_total/numprocs;
    /* Time taken by MPI_Test calls */
    test_total = test_total/numprocs;
    /* Pure Communication Time */
    comm_time = comm_time/numprocs;
    /* Time for MPI_Wait() call */
    wait_total = wait_total/numprocs;
    /* Time for the NBC call */
    init_total = init_total/numprocs;

    print_stats_nbc(rank, size, overall_time, tcomp_total, comm_time,
                    wait_total, init_total, test_total);

}

void print_stats_nbc (int rank, int size, double overall_time,
                 double cpu_time, double comm_time,
                 double wait_time, double init_time,
                 double test_time)
{
    if (rank) {
        return;
    }

    double overlap;

    /* Note : cpu_time received in this function includes time for
       *      dummy compute as well as test calls so we will subtract
       *      the test_time for overlap calculation as test is an
       *      overhead
       */

    overlap = MAX(0, 100 - (((overall_time - (cpu_time - test_time)) / comm_time) * 100));

    if (options.show_size) {
        fprintf(stdout, "%-*d", 10, size);
        fprintf(stdout, "%*.*f", FIELD_WIDTH, FLOAT_PRECISION, overall_time);
    } else {
        fprintf(stdout, "%*.*f", FIELD_WIDTH, FLOAT_PRECISION, overall_time);
    }

    if (options.show_full) {
        fprintf(stdout, "%*.*f%*.*f%*.*f%*.*f%*.*f%*.*f\n",
                FIELD_WIDTH, FLOAT_PRECISION, (cpu_time - test_time),
                FIELD_WIDTH, FLOAT_PRECISION, init_time,
                FIELD_WIDTH, FLOAT_PRECISION, test_time,
                FIELD_WIDTH, FLOAT_PRECISION, wait_time,
                FIELD_WIDTH, FLOAT_PRECISION, comm_time,
                FIELD_WIDTH, FLOAT_PRECISION, overlap);
    } else {
        fprintf(stdout, "%*.*f", FIELD_WIDTH, FLOAT_PRECISION, (cpu_time - test_time));
        fprintf(stdout, "%*.*f", FIELD_WIDTH, FLOAT_PRECISION, comm_time);
        fprintf(stdout, "%*.*f\n", FIELD_WIDTH, FLOAT_PRECISION, overlap);
    }

    fflush(stdout);
}

void print_stats (int rank, int size, double avg_time, double min_time, double max_time)
{
    if (rank) {
        return;
    }

    if (options.show_size) {
        fprintf(stdout, "%-*d", 10, size);
        fprintf(stdout, "%*.*f", FIELD_WIDTH, FLOAT_PRECISION, avg_time);
    } else {
        fprintf(stdout, "%*.*f", 17, FLOAT_PRECISION, avg_time);
    }

    if (options.show_full) {
        fprintf(stdout, "%*.*f%*.*f%*lu\n",
                FIELD_WIDTH, FLOAT_PRECISION, min_time,
                FIELD_WIDTH, FLOAT_PRECISION, max_time,
                12, options.iterations);
    } else {
        fprintf(stdout, "\n");
    }

    fflush(stdout);
}

void set_buffer_pt2pt (void * buffer, int rank, enum accel_type type, int data, size_t size)
{
    char buf_type = 'H';

    if (options.bench == MBW_MR) {
        buf_type = (rank < options.pairs) ? options.src : options.dst;
    } else {
        buf_type = (rank == 0) ? options.src : options.dst;
    }

    switch (buf_type) {
        case 'H':
            memset(buffer, data, size);
            break;
        case 'D':
        case 'M':
#ifdef _ENABLE_OPENACC_
            if (type == OPENACC) {
                size_t i;
                char * p = (char *)buffer;
                #pragma acc parallel loop deviceptr(p)
                for (i = 0; i < size; i++) {
                    p[i] = data;
                }
                break;
            } else
#endif
#ifdef _ENABLE_CUDA_
            {
                CUDA_CHECK(cudaMemset(buffer, data, size));
            }
#endif
            break;
    }
}

void set_buffer (void * buffer, enum accel_type type, int data, size_t size)
{
#ifdef _ENABLE_OPENACC_
    size_t i;
    char * p = (char *)buffer;
#endif
    switch (type) {
        case NONE:
            memset(buffer, data, size);
            break;
        case CUDA:
        case MANAGED:
#ifdef _ENABLE_CUDA_
            CUDA_CHECK(cudaMemset(buffer, data, size));
#endif
            break;
        case OPENACC:
#ifdef _ENABLE_OPENACC_
            #pragma acc parallel loop deviceptr(p)
            for (i = 0; i < size; i++) {
                p[i] = data;
            }
#endif
            break;
    }
}

int allocate_memory_coll (void ** buffer, size_t size, enum accel_type type)
{
    if (options.target == CPU || options.target == BOTH) {
        allocate_host_arrays();
    }

    size_t alignment = sysconf(_SC_PAGESIZE);

    switch (type) {
        case NONE:
            return posix_memalign(buffer, alignment, size);
#ifdef _ENABLE_CUDA_
        case CUDA:
            CUDA_CHECK(cudaMalloc(buffer, size));
            return 0;
        case MANAGED:
            CUDA_CHECK(cudaMallocManaged(buffer, size, cudaMemAttachGlobal));
            return 0;
#endif
#ifdef _ENABLE_OPENACC_
        case OPENACC:
            *buffer = acc_malloc(size);
            if (NULL == *buffer) {
                return 1;
            } else {
                return 0;
            }
#endif
        default:
            return 1;
    }
}

int allocate_device_buffer (char ** buffer)
{
    switch (options.accel) {
#ifdef _ENABLE_CUDA_
        case CUDA:
             CUDA_CHECK(cudaMalloc((void **)buffer, options.max_message_size));
            break;
#endif
#ifdef _ENABLE_OPENACC_
        case OPENACC:
            *buffer = acc_malloc(options.max_message_size);
            if (NULL == *buffer) {
                fprintf(stderr, "Could not allocate device memory\n");
                return 1;
            }
            break;
#endif
        default:
            fprintf(stderr, "Could not allocate device memory\n");
            return 1;
    }

    return 0;
}

int allocate_device_buffer_one_sided (char ** buffer, size_t size)
{
    switch (options.accel) {
#ifdef _ENABLE_CUDA_
        case CUDA:
            CUDA_CHECK(cudaMalloc((void **)buffer, size));
            break;
        case MANAGED:
            CUDA_CHECK(cudaMallocManaged((void **)buffer, size, cudaMemAttachGlobal));
            break;
#endif
#ifdef _ENABLE_OPENACC_
        case OPENACC:
            *buffer = acc_malloc(size);
            if (NULL == *buffer) {
                fprintf(stderr, "Could not allocate device memory\n");
                return 1;
            }
            break;
#endif
        default:
            fprintf(stderr, "Could not allocate device memory\n");
            return 1;
    }

    return 0;
}

int allocate_managed_buffer (char ** buffer)
{
    switch (options.accel) {
#ifdef _ENABLE_CUDA_
        case CUDA:
            CUDA_CHECK(cudaMallocManaged((void **)buffer, options.max_message_size, cudaMemAttachGlobal));
            break;
#endif
        default:
            fprintf(stderr, "Could not allocate device memory\n");
            return 1;

    }
    return 0;
}

int allocate_memory_pt2pt_mul (char ** sbuf, char ** rbuf, int rank, int pairs)
{
    unsigned long align_size = sysconf(_SC_PAGESIZE);

    if (rank < pairs) {
        if ('D' == options.src) {
            if (allocate_device_buffer(sbuf)) {
                fprintf(stderr, "Error allocating cuda memory\n");
                return 1;
            }

            if (allocate_device_buffer(rbuf)) {
                fprintf(stderr, "Error allocating cuda memory\n");
                return 1;
            }
        } else if ('M' == options.src) {
            if (allocate_managed_buffer(sbuf)) {
                fprintf(stderr, "Error allocating cuda unified memory\n");
                return 1;
            }

            if (allocate_managed_buffer(rbuf)) {
                fprintf(stderr, "Error allocating cuda unified memory\n");
                return 1;
            }
        } else {
            if (posix_memalign((void**)sbuf, align_size, options.max_message_size)) {
                fprintf(stderr, "Error allocating host memory\n");
                return 1;
            }

            if (posix_memalign((void**)rbuf, align_size, options.max_message_size)) {
                fprintf(stderr, "Error allocating host memory\n");
                return 1;
            }

            memset(*sbuf, 0, options.max_message_size);
            memset(*rbuf, 0, options.max_message_size);
        }
    } else {
        if ('D' == options.dst) {
            if (allocate_device_buffer(sbuf)) {
                fprintf(stderr, "Error allocating cuda memory\n");
                return 1;
            }

            if (allocate_device_buffer(rbuf)) {
                fprintf(stderr, "Error allocating cuda memory\n");
                return 1;
            }
        } else if ('M' == options.dst) {
            if (allocate_managed_buffer(sbuf)) {
                fprintf(stderr, "Error allocating cuda unified memory\n");
                return 1;
            }

            if (allocate_managed_buffer(rbuf)) {
                fprintf(stderr, "Error allocating cuda unified memory\n");
                return 1;
            }
        } else {
            if (posix_memalign((void**)sbuf, align_size, options.max_message_size)) {
                fprintf(stderr, "Error allocating host memory\n");
                return 1;
            }

            if (posix_memalign((void**)rbuf, align_size, options.max_message_size)) {
                fprintf(stderr, "Error allocating host memory\n");
                return 1;
            }
            memset(*sbuf, 0, options.max_message_size);
            memset(*rbuf, 0, options.max_message_size);
        }
    }

    return 0;
}

int allocate_memory_pt2pt (char ** sbuf, char ** rbuf, int rank)
{
    unsigned long align_size = sysconf(_SC_PAGESIZE);

    switch (rank) {
        case 0:
            if ('D' == options.src) {
                if (allocate_device_buffer(sbuf)) {
                    fprintf(stderr, "Error allocating cuda memory\n");
                    return 1;
                }

                if (allocate_device_buffer(rbuf)) {
                    fprintf(stderr, "Error allocating cuda memory\n");
                    return 1;
                }
            } else if ('M' == options.src) {
                if (allocate_managed_buffer(sbuf)) {
                    fprintf(stderr, "Error allocating cuda unified memory\n");
                    return 1;
                }

                if (allocate_managed_buffer(rbuf)) {
                    fprintf(stderr, "Error allocating cuda unified memory\n");
                    return 1;
                }
            } else {
                if (posix_memalign((void**)sbuf, align_size, options.max_message_size)) {
                    fprintf(stderr, "Error allocating host memory\n");
                    return 1;
                }

                if (posix_memalign((void**)rbuf, align_size, options.max_message_size)) {
                    fprintf(stderr, "Error allocating host memory\n");
                    return 1;
                }
            }
            break;
        case 1:
            if ('D' == options.dst) {
                if (allocate_device_buffer(sbuf)) {
                    fprintf(stderr, "Error allocating cuda memory\n");
                    return 1;
                }

                if (allocate_device_buffer(rbuf)) {
                    fprintf(stderr, "Error allocating cuda memory\n");
                    return 1;
                }
            } else if ('M' == options.dst) {
                if (allocate_managed_buffer(sbuf)) {
                    fprintf(stderr, "Error allocating cuda unified memory\n");
                    return 1;
                }

                if (allocate_managed_buffer(rbuf)) {
                    fprintf(stderr, "Error allocating cuda unified memory\n");
                    return 1;
                }
            } else {
                if (posix_memalign((void**)sbuf, align_size, options.max_message_size)) {
                    fprintf(stderr, "Error allocating host memory\n");
                    return 1;
                }

                if (posix_memalign((void**)rbuf, align_size, options.max_message_size)) {
                    fprintf(stderr, "Error allocating host memory\n");
                    return 1;
                }
            }
            break;
    }

    return 0;
}

void allocate_memory_one_sided(int rank, char **sbuf, char **rbuf,
        char **win_base, size_t size, enum WINDOW type, MPI_Win *win)
{
    int page_size;
    int mem_on_dev = 0;

    page_size = getpagesize();
    assert(page_size <= MAX_ALIGNMENT);

    if (rank == 0) {
        mem_on_dev = ('H' == options.src) ? 0 : 1;
    } else {
        mem_on_dev = ('H' == options.dst) ? 0 : 1;
    }

    if (mem_on_dev) {
        CHECK(allocate_device_buffer_one_sided(sbuf, size));
        set_device_memory(*sbuf, 'a', size);
        CHECK(allocate_device_buffer_one_sided(rbuf, size));
        set_device_memory(*rbuf, 'b', size);
    } else {
        CHECK(posix_memalign((void **)sbuf, page_size, size));
        memset(*sbuf, 'a', size);
        CHECK(posix_memalign((void **)rbuf, page_size, size));
        memset(*rbuf, 'b', size);
    }

#if MPI_VERSION >= 3
    MPI_Status  reqstat;

    switch (type) {
        case WIN_CREATE:
            MPI_CHECK(MPI_Win_create(*win_base, size, 1, MPI_INFO_NULL, MPI_COMM_WORLD, win));
            break;
        case WIN_DYNAMIC:
            MPI_CHECK(MPI_Win_create_dynamic(MPI_INFO_NULL, MPI_COMM_WORLD, win));
            MPI_CHECK(MPI_Win_attach(*win, (void *)*win_base, size));
            MPI_CHECK(MPI_Get_address(*win_base, &disp_local));
            if(rank == 0){
                MPI_CHECK(MPI_Send(&disp_local, 1, MPI_AINT, 1, 1, MPI_COMM_WORLD));
                MPI_CHECK(MPI_Recv(&disp_remote, 1, MPI_AINT, 1, 1, MPI_COMM_WORLD, &reqstat));
            } else {
                MPI_CHECK(MPI_Recv(&disp_remote, 1, MPI_AINT, 0, 1, MPI_COMM_WORLD, &reqstat));
                MPI_CHECK(MPI_Send(&disp_local, 1, MPI_AINT, 0, 1, MPI_COMM_WORLD));
            }
            break;
        default:
            if (mem_on_dev) {
                MPI_CHECK(MPI_Win_create(*win_base, size, 1, MPI_INFO_NULL, MPI_COMM_WORLD, win));
            } else {
                MPI_CHECK(MPI_Win_allocate(size, 1, MPI_INFO_NULL, MPI_COMM_WORLD, *win_base, win));
            }
            break;
    }
#else
    MPI_CHECK(MPI_Win_create(*win_base, size, 1, MPI_INFO_NULL, MPI_COMM_WORLD, win));
#endif
}

void free_buffer (void * buffer, enum accel_type type)
{
    switch (type) {
        case NONE:
            free(buffer);
            break;
        case MANAGED:
        case CUDA:
#ifdef _ENABLE_CUDA_
            CUDA_CHECK(cudaFree(buffer));
#endif
            break;
        case OPENACC:
#ifdef _ENABLE_OPENACC_
            acc_free(buffer);
#endif
            break;
    }

    /* Free dummy compute related resources */
    if (CPU == options.target || BOTH == options.target) {
        free_host_arrays();
    }

    if (GPU == options.target || BOTH == options.target) {
#ifdef _ENABLE_CUDA_KERNEL_
        free_device_arrays();
#endif /* #ifdef _ENABLE_CUDA_KERNEL_ */
    }
}

#if defined(_ENABLE_OPENACC_) || defined(_ENABLE_CUDA_)
int omb_get_local_rank()
{
    char *str = NULL;
    int local_rank = -1;

    if ((str = getenv("MV2_COMM_WORLD_LOCAL_RANK")) != NULL) {
        local_rank = atoi(str);
    } else if ((str = getenv("OMPI_COMM_WORLD_LOCAL_RANK")) != NULL) {
        local_rank = atoi(str);
    } else if ((str = getenv("LOCAL_RANK")) != NULL) {
        local_rank = atoi(str);
    } else {
        fprintf(stderr, "Warning: OMB could not identify the local rank of the process.\n");
        fprintf(stderr, "         This can lead to multiple processes using the same GPU.\n");
        fprintf(stderr, "         Please use the get_local_rank script in the OMB repo for this.\n");
    }

    return local_rank;
}
#endif /* defined(_ENABLE_OPENACC_) || defined(_ENABLE_CUDA_) */

int init_accel (void)
{
#ifdef _ENABLE_CUDA_
    CUresult curesult = CUDA_SUCCESS;
    CUdevice cuDevice;
#endif
#if defined(_ENABLE_OPENACC_) || defined(_ENABLE_CUDA_)
    int local_rank = -1, dev_count = 0;
    int dev_id = 0;

    local_rank = omb_get_local_rank();
#endif

    switch (options.accel) {
#ifdef _ENABLE_CUDA_
        case MANAGED:
        case CUDA:
            if (local_rank >= 0) {
                CUDA_CHECK(cudaGetDeviceCount(&dev_count));
                dev_id = local_rank % dev_count;
            }

            curesult = cuInit(0);
            if (curesult != CUDA_SUCCESS) {
                return 1;
            }

            curesult = cuDeviceGet(&cuDevice, dev_id);
            if (curesult != CUDA_SUCCESS) {
                return 1;
            }

            curesult = cuCtxCreate(&cuContext, 0, cuDevice);
            if (curesult != CUDA_SUCCESS) {
                return 1;
            }
            break;
#endif
#ifdef _ENABLE_OPENACC_
        case OPENACC:
            if (local_rank >= 0) {
                dev_count = acc_get_num_devices(acc_device_not_host);
                assert(dev_count > 0);
                dev_id = local_rank % dev_count;
            }

            acc_set_device_num (dev_id, acc_device_not_host);
            break;
#endif
        default:
            fprintf(stderr, "Invalid device type, should be cuda or openacc\n");
            return 1;
    }

    return 0;
}

int cleanup_accel (void)
{
#ifdef _ENABLE_CUDA_
    CUresult curesult = CUDA_SUCCESS;
#endif

    switch (options.accel) {
#ifdef _ENABLE_CUDA_
        case MANAGED:
        case CUDA:
            curesult = cuCtxDestroy(cuContext);

            if (curesult != CUDA_SUCCESS) {
                return 1;
            }
            break;
#endif
#ifdef _ENABLE_OPENACC_
        case OPENACC:
            acc_shutdown(acc_device_nvidia);
            break;
#endif
        default:
            fprintf(stderr, "Invalid accel type, should be cuda or openacc\n");
            return 1;
    }

    return 0;
}

#ifdef _ENABLE_CUDA_KERNEL_
void free_device_arrays()
{
    if (is_alloc) {
        CUDA_CHECK(cudaFree(d_x));
        CUDA_CHECK(cudaFree(d_y));

        is_alloc = 0;
    }
}
#endif

void free_host_arrays()
{
    int i = 0;

    if (x) {
        free(x);
    }
    if (y) {
        free(y);
    }

    if (a) {
        for (i = 0; i < DIM; i++) {
            free(a[i]);
        }
        free(a);
    }

    x = NULL;
    y = NULL;
    a = NULL;
}

void free_memory (void * sbuf, void * rbuf, int rank)
{
    switch (rank) {
        case 0:
            if ('D' == options.src || 'M' == options.src) {
                free_device_buffer(sbuf);
                free_device_buffer(rbuf);
            } else {
                free(sbuf);
                free(rbuf);
            }
            break;
        case 1:
            if ('D' == options.dst || 'M' == options.dst) {
                free_device_buffer(sbuf);
                free_device_buffer(rbuf);
            } else {
                free(sbuf);
                free(rbuf);
            }
            break;
    }
}

void free_memory_pt2pt_mul (void * sbuf, void * rbuf, int rank, int pairs)
{
    if (rank < pairs) {
        if ('D' == options.src || 'M' == options.src) {
            free_device_buffer(sbuf);
            free_device_buffer(rbuf);
        } else {
            free(sbuf);
            free(rbuf);
        }
    } else {
        if ('D' == options.dst || 'M' == options.dst) {
            free_device_buffer(sbuf);
            free_device_buffer(rbuf);
        } else {
            free(sbuf);
            free(rbuf);
        }
    }
}

void free_memory_one_sided (void *sbuf, void *rbuf, MPI_Win win, int rank)
{
    MPI_CHECK(MPI_Win_free(&win));
    free_memory(sbuf, rbuf, rank);
}

double dummy_compute(double seconds, MPI_Request* request)
{
    double test_time = 0.0;

    test_time = do_compute_and_probe(seconds, request);

    return test_time;
}

#ifdef _ENABLE_CUDA_KERNEL_
void do_compute_gpu(double seconds)
{
    double time_elapsed = 0.0, t1 = 0.0, t2 = 0.0;

    {
        t1 = MPI_Wtime();

        /* Execute Dummy Kernel on GPU if set by user */
        if (options.target == BOTH || options.target == GPU) {
            {
                CUDA_CHECK(cudaStreamCreate(&stream));
                call_kernel(A, d_x, d_y, options.device_array_size, &stream);
            }
        }

        t2 = MPI_Wtime();
        time_elapsed += (t2-t1);
    }
}
#endif

void
compute_on_host()
{
    int i = 0, j = 0;
    for (i = 0; i < DIM; i++)
        for (j = 0; j < DIM; j++)
            x[i] = x[i] + a[i][j]*a[j][i] + y[j];
}

static inline void do_compute_cpu(double target_seconds)
{
    double t1 = 0.0, t2 = 0.0;
    double time_elapsed = 0.0;
    while (time_elapsed < target_seconds) {
        t1 = MPI_Wtime();
        compute_on_host();
        t2 = MPI_Wtime();
        time_elapsed += (t2-t1);
    }
    if (DEBUG) {
        fprintf(stderr, "time elapsed = %f\n", (time_elapsed * 1e6));
    }
}

double do_compute_and_probe(double seconds, MPI_Request* request)
{
    double t1 = 0.0, t2 = 0.0;
    double test_time = 0.0;
    int num_tests = 0;
    double target_seconds_for_compute = 0.0;
    int flag = 0;
    MPI_Status status;

    if (options.num_probes) {
        target_seconds_for_compute = (double) seconds/options.num_probes;
        if (DEBUG) {
            fprintf(stderr, "setting target seconds to %f\n", (target_seconds_for_compute * 1e6 ));
        }
    } else {
        target_seconds_for_compute = seconds;
        if (DEBUG) {
            fprintf(stderr, "setting target seconds to %f\n", (target_seconds_for_compute * 1e6 ));
        }
    }

#ifdef _ENABLE_CUDA_KERNEL_
    if (options.target == GPU) {
        if (options.num_probes) {
            /* Do the dummy compute on GPU only */
            do_compute_gpu(target_seconds_for_compute);
            num_tests = 0;
            while (num_tests < options.num_probes) {
                t1 = MPI_Wtime();
                MPI_CHECK(MPI_Test(request, &flag, &status));
                t2 = MPI_Wtime();
                test_time += (t2-t1);
                num_tests++;
            }
        } else {
            do_compute_gpu(target_seconds_for_compute);
        }
    } else if (options.target == BOTH) {
        if (options.num_probes) {
            /* Do the dummy compute on GPU and CPU*/
            do_compute_gpu(target_seconds_for_compute);
            num_tests = 0;
            while (num_tests < options.num_probes) {
                t1 = MPI_Wtime();
                MPI_CHECK(MPI_Test(request, &flag, &status));
                t2 = MPI_Wtime();
                test_time += (t2-t1);
                num_tests++;
                do_compute_cpu(target_seconds_for_compute);
            }
        } else {
            do_compute_gpu(target_seconds_for_compute);
            do_compute_cpu(target_seconds_for_compute);
        }
    } else
#endif
    if (options.target == CPU) {
        if (options.num_probes) {
            num_tests = 0;
            while (num_tests < options.num_probes) {
                do_compute_cpu(target_seconds_for_compute);
                t1 = MPI_Wtime();
                MPI_CHECK(MPI_Test(request, &flag, &status));
                t2 = MPI_Wtime();
                test_time += (t2-t1);
                num_tests++;
            }
        } else {
            do_compute_cpu(target_seconds_for_compute);
        }
    }

#ifdef _ENABLE_CUDA_KERNEL_
    if (options.target == GPU || options.target == BOTH) {
        CUDA_CHECK(cudaDeviceSynchronize());
        CUDA_CHECK(cudaStreamDestroy(stream));
    }
#endif

    return test_time;
}

void allocate_host_arrays()
{
    int i=0, j=0;
    a = (float **)malloc(DIM * sizeof(float *));

    for (i = 0; i < DIM; i++) {
        a[i] = (float *)malloc(DIM * sizeof(float));
    }

    x = (float *)malloc(DIM * sizeof(float));
    y = (float *)malloc(DIM * sizeof(float));

    for (i = 0; i < DIM; i++) {
        x[i] = y[i] = 1.0f;
        for (j = 0; j < DIM; j++) {
            a[i][j] = 2.0f;
        }
    }
}

void allocate_atomic_memory(int rank,
        char **sbuf, char **rbuf, char **tbuf, char **cbuf,
        char **win_base, size_t size, enum WINDOW type, MPI_Win *win)
{
    int page_size;
    int mem_on_dev = 0;

    page_size = getpagesize();
    assert(page_size <= MAX_ALIGNMENT);

    if (rank == 0) {
        mem_on_dev = ('D' == options.src) ? 1 : 0;
    } else {
        mem_on_dev = ('D' == options.dst) ? 1 : 0;
    }

    if (mem_on_dev) {
        CHECK(allocate_device_buffer(sbuf));
        set_device_memory(*sbuf, 'a', size);
        CHECK(allocate_device_buffer(rbuf));
        set_device_memory(*rbuf, 'b', size);
        CHECK(allocate_device_buffer(tbuf));
        set_device_memory(*tbuf, 'c', size);
        if (cbuf != NULL) {
            CHECK(allocate_device_buffer(cbuf));
            set_device_memory(*cbuf, 'a', size);
        }
    } else {
        CHECK(posix_memalign((void **)sbuf, page_size, size));
        memset(*sbuf, 'a', size);
        CHECK(posix_memalign((void **)rbuf, page_size, size));
        memset(*rbuf, 'b', size);
        CHECK(posix_memalign((void **)tbuf, page_size, size));
        memset(*tbuf, 'c', size);
        if (cbuf != NULL) {
            CHECK(posix_memalign((void **)cbuf, page_size, size));
            memset(*cbuf, 'a', size);
        }
    }

#if MPI_VERSION >= 3
    MPI_Status  reqstat;

    switch (type) {
        case WIN_CREATE:
            MPI_CHECK(MPI_Win_create(*win_base, size, 1, MPI_INFO_NULL, MPI_COMM_WORLD, win));
            break;
        case WIN_DYNAMIC:
            MPI_CHECK(MPI_Win_create_dynamic(MPI_INFO_NULL, MPI_COMM_WORLD, win));
            MPI_CHECK(MPI_Win_attach(*win, (void *)*win_base, size));
            MPI_CHECK(MPI_Get_address(*win_base, &disp_local));
            if(rank == 0){
                MPI_CHECK(MPI_Send(&disp_local, 1, MPI_AINT, 1, 1, MPI_COMM_WORLD));
                MPI_CHECK(MPI_Recv(&disp_remote, 1, MPI_AINT, 1, 1, MPI_COMM_WORLD, &reqstat));
            } else {
                MPI_CHECK(MPI_Recv(&disp_remote, 1, MPI_AINT, 0, 1, MPI_COMM_WORLD, &reqstat));
                MPI_CHECK(MPI_Send(&disp_local, 1, MPI_AINT, 0, 1, MPI_COMM_WORLD));
            }
            break;
        default:
            if (mem_on_dev) {
                MPI_CHECK(MPI_Win_create(*win_base, size, 1, MPI_INFO_NULL, MPI_COMM_WORLD, win));
            } else {
                MPI_CHECK(MPI_Win_allocate(size, 1, MPI_INFO_NULL, MPI_COMM_WORLD, *win_base, win));
            }
            break;
    }
#else
    MPI_CHECK(MPI_Win_create(*win_base, size, 1, MPI_INFO_NULL, MPI_COMM_WORLD, win));
#endif
}

void free_atomic_memory (void *sbuf, void *rbuf, void *tbuf, void *cbuf, MPI_Win win, int rank)
{
    int mem_on_dev = 0;
    MPI_CHECK(MPI_Win_free(&win));

    if (rank == 0) {
        mem_on_dev = ('D' == options.src) ? 1 : 0;
    } else {
        mem_on_dev = ('D' == options.dst) ? 1 : 0;
    }

    if (mem_on_dev) {
        free_device_buffer(sbuf);
        free_device_buffer(rbuf);
        free_device_buffer(tbuf);
        if (cbuf != NULL) {
            free_device_buffer(cbuf);
        }
    } else {
        free(sbuf);
        free(rbuf);
        free(tbuf);
        if (cbuf != NULL) {
            free(cbuf);
        }
    }
}

void init_arrays(double target_time)
{

    if (DEBUG) {
        fprintf(stderr, "called init_arrays with target_time = %f \n",
                (target_time * 1e6));
    }

#ifdef _ENABLE_CUDA_KERNEL_
    if (options.target == GPU || options.target == BOTH) {
    /* Setting size of arrays for Dummy Compute */
    int N = options.device_array_size;

    /* Device Arrays for Dummy Compute */
    allocate_device_arrays(N);

    double t1 = 0.0, t2 = 0.0;

    while (1) {
        t1 = MPI_Wtime();

        if (options.target == GPU || options.target == BOTH) {
            CUDA_CHECK(cudaStreamCreate(&stream));
            call_kernel(A, d_x, d_y, N, &stream);

            CUDA_CHECK(cudaDeviceSynchronize());
            CUDA_CHECK(cudaStreamDestroy(stream));
        }

        t2 = MPI_Wtime();
        if ((t2-t1) < target_time)
        {
            N += 32;

            /* Now allocate arrays of size N */
            allocate_device_arrays(N);
        } else {
            break;
        }
    }

    /* we reach here with desired N so save it and pass it to options */
    options.device_array_size = N;
    if (DEBUG) {
        fprintf(stderr, "correct N = %d\n", N);
        }
    }
#endif

}

#ifdef _ENABLE_CUDA_KERNEL_
void allocate_device_arrays(int n)
{
    /* First free the old arrays */
    free_device_arrays();

    /* Allocate Device Arrays for Dummy Compute */
    CUDA_CHECK(cudaMalloc((void**)&d_x, n * sizeof(float)));

    CUDA_CHECK(cudaMalloc((void**)&d_y, n * sizeof(float)));

    CUDA_CHECK(cudaMemset(d_x, 1.0f, n));
    CUDA_CHECK(cudaMemset(d_y, 2.0f, n));
    is_alloc = 1;
}
#endif
/* vi:set sw=4 sts=4 tw=80: */

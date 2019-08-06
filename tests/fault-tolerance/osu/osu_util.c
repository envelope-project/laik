/*
 * Copyright (C) 2002-2019 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University.
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level directory.
 */

#include "osu_util.h"

#ifdef _ENABLE_OPENACC_
#include <openacc.h>
#endif

/*
 * GLOBAL VARIABLES
 */
char const * benchmark_header = NULL;
char const * benchmark_name = NULL;
int accel_enabled = 0;
struct options_t options;

struct bad_usage_t bad_usage;

void
print_header(int rank, int full)
{
    switch(options.bench) {
        case MBW_MR :
        case PT2PT :
            if (0 == rank) {
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

                switch (options.accel) {
                    case CUDA:
                    case OPENACC:
                        fprintf(stdout, "# Send Buffer on %s and Receive Buffer on %s\n",
                               'M' == options.src ? "MANAGED (M)" : ('D' == options.src ? "DEVICE (D)" : "HOST (H)"),
                               'M' == options.dst ? "MANAGED (M)" : ('D' == options.dst ? "DEVICE (D)" : "HOST (H)"));
                    default:
                        if (options.subtype == BW && options.bench != MBW_MR) {
                            fprintf(stdout, "%-*s%*s\n", 10, "# Size", FIELD_WIDTH, "Bandwidth (MB/s)");
                        }
                        else {
                            fprintf(stdout, "%-*s%*s\n", 10, "# Size", FIELD_WIDTH, "Latency (us)");
                        }
                        fflush(stdout);
                }
            }
            break;
        case COLLECTIVE :
            if(rank == 0) {
                fprintf(stdout, HEADER, "");

                if (options.show_size) {
                    fprintf(stdout, "%-*s", 10, "# Size");
                    fprintf(stdout, "%*s", FIELD_WIDTH, "Avg Latency(us)");
                }

                else {
                    fprintf(stdout, "# Avg Latency(us)");
                }

                if (full) {
                    fprintf(stdout, "%*s", FIELD_WIDTH, "Min Latency(us)");
                    fprintf(stdout, "%*s", FIELD_WIDTH, "Max Latency(us)");
                    fprintf(stdout, "%*s\n", 12, "Iterations");
                }

                else {
                    fprintf(stdout, "\n");
                }

                fflush(stdout);
            }
            break;
        default:
            break;
    }
}

void print_data (int rank, int full, int size, double avg_time,
                 double min_time, double max_time, int iterations)
{
    if(rank == 0) {
        if (options.show_size) {
            fprintf(stdout, "%-*d", 10, size);
            fprintf(stdout, "%*.*f", FIELD_WIDTH, FLOAT_PRECISION, avg_time);
        } else {
            fprintf(stdout, "%*.*f", 17, FLOAT_PRECISION, avg_time);
        }

        if (full) {
            fprintf(stdout, "%*.*f%*.*f%*d\n",
                    FIELD_WIDTH, FLOAT_PRECISION, min_time,
                    FIELD_WIDTH, FLOAT_PRECISION, max_time,
                    12, iterations);
        } else {
            fprintf(stdout, "\n");

        }

        fflush(stdout);
    }
}


static int
set_min_message_size (long long value)
{
    if (0 >= value) {
        return -1;
    }

    options.min_message_size = value;

    return 0;
}

static int
set_max_message_size (long long value)
{
    if (0 > value) {
        return -1;
    }

    options.max_message_size = value;

    return 0;
}

static int
set_message_size (char *val_str)
{
    int retval = -1;
    int i, count = 0;
    char *val1, *val2;

    for (i=0; val_str[i]; i++) {
        if (val_str[i] == ':')
            count++;
    }

    if (!count) {
        retval = set_max_message_size(atoll(val_str));
    } else if (count == 1) {
        val1 = strtok(val_str, ":");
        val2 = strtok(NULL, ":");

        if (val1 && val2) {
            retval = set_min_message_size(atoll(val1));
            retval = set_max_message_size(atoll(val2));
        } else if (val1) {
            if (val_str[0] == ':') {
                retval = set_max_message_size(atoll(val1));
            } else {
                retval = set_min_message_size(atoll(val1));
            }
        }
    }

    return retval;
}

static int
set_receiver_threads (int value){
    if (MIN_NUM_THREADS > value || value>=MAX_NUM_THREADS) {
        return -1;
    }

    options.num_threads = value;

    return 0;

}

static int
set_sender_threads (int value){
    if (MIN_NUM_THREADS > value || value>=MAX_NUM_THREADS) {
        return -1;
    }

    options.sender_thread = value;

    return 0;

}
static int
set_threads (char *val_str)
{
    int retval = -1;
    int i, count = 0;
    char *val1, *val2;

    for (i=0; val_str[i]; i++) {
        if (val_str[i] == ':')
            count++;
    }

    if (!count) {
        retval = set_receiver_threads(atoi(val_str));
        options.sender_thread=-1;
    } else if (count == 1) {
        val1 = strtok(val_str, ":");
        val2 = strtok(NULL, ":");

        if (val1 && val2) {
            retval = set_sender_threads(atoi(val1));
            if(retval==-1){
                return retval;
            }
            retval = set_receiver_threads(atoi(val2));
        } 
        
    }

    return retval;
}

static int set_num_warmup (int value)
{
    if (0 > value) {
        return -1;
    }

    options.skip = value;
    options.skip_large = value;

    return 0;
}

static int set_num_iterations (int value)
{
    if (1 > value) {
        return -1;
    }

    options.iterations = value;
    options.iterations_large = value;

    return 0;
}

static int set_window_size (int value)
{
    if (1 > value) {
        return -1;
    }

    options.window_size = value;

    return 0;
}

static int set_device_array_size (int value)
{
    if (value < 1 ) {
        return -1;
    }

    options.device_array_size = value;

    return 0;
}

static int set_num_probes (int value)
{
    if (value < 0 ) {
        return -1;
    }

    options.num_probes = value;

    return 0;
}

static int set_max_memlimit (long long value)
{
    options.max_mem_limit = value;

    if (value < MAX_MEM_LOWER_LIMIT) {
        options.max_mem_limit = MAX_MEM_LOWER_LIMIT;
        fprintf(stderr,"Requested memory limit too low, using [%d] instead.",
                MAX_MEM_LOWER_LIMIT);
    }

    return 0;
}

void set_header (const char * header)
{
    benchmark_header = header;
}

void set_benchmark_name (const char * name)
{
    benchmark_name = name;
}

void enable_accel_support (void)
{
    accel_enabled = (CUDA_ENABLED || OPENACC_ENABLED);
}

int process_options (int argc, char *argv[])
{
    extern char * optarg;
    extern int optind, optopt;

    char const * optstring = NULL;
    int c, ret = PO_OKAY;

    int option_index = 0;

    static struct option long_options[] = {
            {"help",            no_argument,        0,  'h'},
            {"version",         no_argument,        0,  'v'},
            {"full",            no_argument,        0,  'f'},
            {"message-size",    required_argument,  0,  'm'},
            {"window-size",     required_argument,  0,  'W'},
            {"num-test-calls",  required_argument,  0,  't'},
            {"iterations",      required_argument,  0,  'i'},
            {"warmup",          required_argument,  0,  'x'},
            {"array-size",      required_argument,  0,  'a'},
            {"sync-option",     required_argument,  0,  's'},
            {"win-options",     required_argument,  0,  'w'},
            {"mem-limit",       required_argument,  0,  'M'},
            {"accelerator",     required_argument,  0,  'd'},
            {"cuda-target",     required_argument,  0,  'r'},
            {"print-rate",      required_argument,  0,  'R'},
            {"num-pairs",       required_argument,  0,  'p'},
            {"vary-window",     required_argument,  0,  'V'}
            
    };

    enable_accel_support();

    if(options.bench == PT2PT) {
        if (accel_enabled) {
            if (options.subtype == LAT_MT) {
                optstring = "+:x:i:t:m:d:hv";
                accel_enabled = 0;
            } else if (options.subtype == BW) {
                optstring = "+:x:i:t:m:d:W:hv";
            } else {
                optstring = "+:x:i:m:d:hv";
            }
        } else{
            if (options.subtype == LAT_MT) {
                optstring = "+:hvm:x:i:t:";
            } else if (options.subtype == BW) {
                optstring = "+:hvm:x:i:t:W:";
            } else {
                optstring = "+:hvm:x:i:";
            }
        }
    } else if (options.bench == COLLECTIVE) {
        if (options.subtype == LAT) { /* Blocking */
            optstring = "+:hvfm:i:x:M:a:";
            if (accel_enabled) {
                optstring = (CUDA_KERNEL_ENABLED) ? "+:d:hvfm:i:x:M:r:a:" : "+:d:hvfm:i:x:M:a:";
            }
        } else { /* Non-Blocking */
            optstring = "+:hvfm:i:x:M:t:a:";
            if (accel_enabled) {
                optstring = (CUDA_KERNEL_ENABLED) ? "+:d:hvfm:i:x:M:t:r:a:" : "+:d:hvfm:i:x:M:t:a:";
            }
        }
    } else if (options.bench == ONE_SIDED) {
        optstring = (accel_enabled) ? "+:w:s:hvm:d:x:i:" : "+:w:s:hvm:x:i:";
    } else if (options.bench == MBW_MR){
        optstring = (accel_enabled) ? "p:W:R:x:i:m:d:Vhv" : "p:W:R:x:i:m:Vhv";
    } else if (options.bench == OSHM || options.bench == UPC || options.bench == UPCXX) {
        optstring = ":hvfm:i:M:";
    } else {
        fprintf(stderr,"Invalid benchmark type");
        exit(1);
    }

    /* Set default options*/
    options.accel = NONE;
    options.show_size = 1;
    options.show_full = 0;
    options.num_probes = 0;
    options.device_array_size = 32;
    options.target = CPU;
    options.min_message_size = MIN_MESSAGE_SIZE;
    if (options.bench == COLLECTIVE) {
        options.max_message_size = MAX_MSG_SIZE_COLL;
    } else {
        options.max_message_size = MAX_MESSAGE_SIZE;
    }
    options.max_mem_limit = MAX_MEM_LIMIT;
    options.window_size_large = WINDOW_SIZE_LARGE;
    options.window_size = WINDOW_SIZE_LARGE;
    options.window_varied = 0;
    options.print_rate = 1;

    options.src = 'H';
    options.dst = 'H';

    switch (options.subtype) {
        case BW:
            options.iterations = BW_LOOP_SMALL;
            options.skip = BW_SKIP_SMALL;
            options.iterations_large = BW_LOOP_LARGE;
            options.skip_large = BW_SKIP_LARGE;
            break;
        case LAT_MT:
            options.num_threads = DEF_NUM_THREADS;
            options.min_message_size = 0;
            options.sender_thread=-1;
        case LAT:
        case NBC:
            if (options.bench == COLLECTIVE) {
                options.iterations = COLL_LOOP_SMALL;
                options.skip = COLL_SKIP_SMALL;
                options.iterations_large = COLL_LOOP_LARGE;
                options.skip_large = COLL_SKIP_LARGE;
            } else {
                options.iterations = LAT_LOOP_SMALL;
                options.skip = LAT_SKIP_SMALL;
                options.iterations_large = LAT_LOOP_LARGE;
                options.skip_large = LAT_SKIP_LARGE;
            }
            if (options.bench == PT2PT) {
                options.min_message_size = 0;
            }
            break;
        default:
            break;
    }

    switch (options.bench) {
        case UPCXX:
        case UPC:
            options.show_size = 0;
        case OSHM:
            options.iterations = OSHM_LOOP_SMALL;
            options.skip = OSHM_SKIP_SMALL;
            options.iterations_large = OSHM_LOOP_LARGE;
            options.skip_large = OSHM_SKIP_LARGE;
            options.max_message_size = 1<<20;
            break;
        default:
            break;
    }

    while ((c = getopt_long(argc, argv, optstring, long_options, &option_index)) != -1) {
        bad_usage.opt = c;
        bad_usage.optarg = NULL;
        bad_usage.message = NULL;

        switch(c) {
            case 'h':
                return PO_HELP_MESSAGE;
            case 'v':
                return PO_VERSION_MESSAGE;
            case 'm':
                if (set_message_size(optarg)) {
                    bad_usage.message = "Invalid Message Size";
                    bad_usage.optarg = optarg;

                    return PO_BAD_USAGE;
                }
                break;
            case 't':
                if (options.bench == COLLECTIVE) {
                    if (set_num_probes(atoi(optarg))){
                        bad_usage.message = "Invalid Number of Probes";
                        bad_usage.optarg = optarg;

                        return PO_BAD_USAGE;
                    }
                } else if (options.bench == PT2PT) {
            
                    if(set_threads(optarg)){
                        bad_usage.message = "Invalid Number of Threads";
                        bad_usage.optarg = optarg;

                        return PO_BAD_USAGE;
                    }
                    
                }
                break;
            case 'i':
                if (set_num_iterations(atoi(optarg))) {
                    bad_usage.message = "Invalid Number of Iterations";
                    bad_usage.optarg = optarg;

                    return PO_BAD_USAGE;
                }
                break;
            case 'x':
                if (set_num_warmup(atoi(optarg))) {
                    bad_usage.message = "Invalid Number of Warmup Iterations";
                    bad_usage.optarg = optarg;

                    return PO_BAD_USAGE;
                }
                break;
            case 'R':
                options.print_rate = atoi(optarg);
                if(0 != options.print_rate && 1 != options.print_rate) {
                    return PO_BAD_USAGE;
                }
                break;
            case 'W':
                if (set_window_size(atoi(optarg))) {
                    bad_usage.message = "Invalid Window Size";
                    bad_usage.optarg = optarg;

                    return PO_BAD_USAGE;
                }
                break;
            case 'V':
                options.window_varied = 1;
                break;
            case 'p':
                options.pairs = atoi(optarg);
                break;
            case 'a':
                if (set_device_array_size(atoi(optarg))){
                    bad_usage.message = "Invalid Device Array Size";
                    bad_usage.optarg = optarg;

                    return PO_BAD_USAGE;
                }
                break;
            case 'f':
                options.show_full = 1;
                break;
            case 'M':
                /*
                 * This function does not error but prints a warning message if
                 * the value is too low.
                 */
                set_max_memlimit(atoll(optarg));
                break;
            case 'd':
                if (!accel_enabled) {
                    bad_usage.message = "Benchmark Does Not Support "
                            "Accelerator Transfers";
                    bad_usage.optarg = optarg;
                    return PO_BAD_USAGE;
                } else if (0 == strncasecmp(optarg, "cuda", 10)) {
                    if (CUDA_ENABLED) {
                        options.accel = CUDA;
                    } else {
                        bad_usage.message = "CUDA Support Not Enabled\n"
                                "Please recompile benchmark with CUDA support";
                        bad_usage.optarg = optarg;
                        return PO_BAD_USAGE;
                    }
                } else if (0 == strncasecmp(optarg, "managed", 10)) {
                    if (CUDA_ENABLED) {
                        options.accel = MANAGED;
                    } else {
                        bad_usage.message = "CUDA Managed Memory Support Not Enabled\n"
                                "Please recompile benchmark with CUDA support";
                        bad_usage.optarg = optarg;
                        return PO_BAD_USAGE;
                    }
                } else if (0 == strncasecmp(optarg, "openacc", 10)) {
                    if (OPENACC_ENABLED) {
                        options.accel = OPENACC;
                    } else {
                        bad_usage.message = "OpenACC Support Not Enabled\n"
                                "Please recompile benchmark with OpenACC support";
                        bad_usage.optarg = optarg;
                        return PO_BAD_USAGE;
                    }
                } else {
                    bad_usage.message = "Invalid Accel Type Specified";
                    bad_usage.optarg = optarg;
                    return PO_BAD_USAGE;
                }
                break;
            case 'r':
                if (CUDA_KERNEL_ENABLED) {
                    if (0 == strncasecmp(optarg, "cpu", 10)) {
                        options.target = CPU;
                    } else if (0 == strncasecmp(optarg, "gpu", 10)) {
                        options.target = GPU;
                    } else if (0 == strncasecmp(optarg, "both", 10)) {
                        options.target = BOTH;
                    } else {
                        bad_usage.message = "Please use cpu, gpu, or both";
                        bad_usage.optarg = optarg;
                        return PO_BAD_USAGE;
                    }
                } else {
                    bad_usage.message = "CUDA Kernel Support Not Enabled\n"
                            "Please recompile benchmark with CUDA Kernel support";
                    bad_usage.optarg = optarg;
                    return PO_BAD_USAGE;
                }
                break;

            case 'w':
                ret = process_one_sided_options(c, optarg);
                if (ret == PO_BAD_USAGE) {
                    bad_usage.message = "Invalid option or invalid argument";
                    bad_usage.opt = c;
                    bad_usage.optarg = optarg;
                    return PO_BAD_USAGE;
                }
                break;
            case 's':
                ret = process_one_sided_options(c, optarg);
                if (ret == PO_BAD_USAGE) {
                    bad_usage.message = "Invalid option or invalid argument";
                    bad_usage.opt = c;
                    bad_usage.optarg = optarg;
                    return PO_BAD_USAGE;
                }
                break;

            case ':':
                bad_usage.message = "Option Missing Required Argument";
                bad_usage.opt = optopt;
                return PO_BAD_USAGE;
            default:
                bad_usage.message = "Invalid option";
                bad_usage.opt = optopt;
                return PO_BAD_USAGE;
        }

    }

    if (accel_enabled) {
        if ((optind + 2) == argc) {
            options.src = argv[optind][0];
            options.dst = argv[optind + 1][0];
            /* No need to check if '-d' is given */
            if (NONE == options.accel) {
                setAccel(options.src);
                setAccel(options.dst);
            }
        } else if (optind != argc) {
            return PO_BAD_USAGE;
        }
    }

    return PO_OKAY;
}

/* Set the initial accelerator type */
int setAccel(char buf_type) {
    switch (buf_type) {
        case 'H':
            break;
        case 'D':
        case 'M':
            if (options.bench != PT2PT && options.bench != ONE_SIDED && options.bench != MBW_MR) {
                bad_usage.opt = buf_type;
                bad_usage.message = "This argument is only supported for one-sided and pt2pt benchmarks";
                return PO_BAD_USAGE;
            }
            if (NONE == options.accel) {
#if defined(_ENABLE_OPENACC_) && !defined(_ENABLE_CUDA_)
                options.accel = OPENACC;
#else
                options.accel = CUDA;
#endif
            }
            break;
        default:
            return PO_BAD_USAGE;
    }
    return PO_OKAY;
}


double getMicrosecondTimeStamp()
{
    double retval;
    struct timeval tv;
    if (gettimeofday(&tv, NULL)) {
        perror("gettimeofday");
        abort();
    }
    retval = ((double)tv.tv_sec) * 1000000 + tv.tv_usec;
    return retval;
}

void wtime(double *t)
{
    static int sec = -1;
    struct timeval tv;
    //gettimeofday(&tv, (void *)0);
    gettimeofday(&tv, 0);
    if (sec < 0) sec = tv.tv_sec;
    *t = (tv.tv_sec - sec)*1.0e+6 + tv.tv_usec;
}


/* vi:set sw=4 sts=4 tw=80: */

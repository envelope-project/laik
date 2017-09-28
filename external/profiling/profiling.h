#ifndef PROFILING_H
#define PROFILING_H

#define MAX_PERF_COUNTERS 128
#define MAX_PERF_NAME_LENGTH 32

typedef struct counter_kvp_tag{
    char name[MAX_PERF_NAME_LENGTH];
    long long value;
}counter_kvp_t;


#endif //PROFILING_H
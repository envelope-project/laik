#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "papi.h"
#include "interface/agent.h"

static int eventset = PAPI_NULL;
static int isInited = 0;
static int ev_count = 0;
static int running = 0;
static long long* values = NULL;
static char events[MAX_PERF_COUNTERS][PAPI_MAX_STR_LEN];

static
void papi_init(
    void
){
    int retval;
    retval = PAPI_library_init(PAPI_VER_CURRENT);
    
    if (retval != PAPI_VER_CURRENT) {
        fprintf(stderr, "PAPI library init error!\n");
        exit(1);
    }

    retval = PAPI_create_eventset(&eventset);

    if(retval!=PAPI_OK){
        fprintf(stderr, "PAPI Eventset creation failed\n");
        exit(1);
    }

    isInited = 1;
}

void prof_detach(
    void
){

}

void prof_reset(
    void
){

}

void prof_get_def(
    long long* total_instructions,
    long long* total_cycle,
    long long* total_flops,
    long long* l2_cache_miss
){
    if(running){

    }
}

int prof_peek(
    void
){

}

void prof_start(
    void
){

}

void prof_stop(
    void
){

}

void prof_add_counter(
    int id
){

}

void prof_get_all_counters(
    int* n_counters, 
    counter_kvp_t* counters
){

}

Laik_Agent* agent_init(
    int argc, 
    char** argv
){
    Laik_Profiling_Agent* me = (Laik_Profiling_Agent*)
            calloc(1, sizeof(Laik_Profiling_Agent));
    assert(me);
    Laik_Agent* myBase = &(me->base);

    myBase->name = "Profiling Interface";
    myBase->id = 0x10;
    myBase->isAlive = 1;
    myBase->isInitialized = 1;
    myBase->type = LAIK_AGENT_PROFILING;

    myBase->detach = prof_detach;
    myBase->reset = prof_reset;

    papi_init();
    papi_add_default_counters();

    me->    

}
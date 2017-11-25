#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <papi.h>
#include "interface/agent.h"

static int eventset = PAPI_NULL;
static int isInited = 0;
static int ev_count = 0;
static int running = 0;
static long long* values = NULL;
static char events[MAX_PERF_COUNTERS][PAPI_MAX_STR_LEN];
static int use_default = 0;


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


double papi_gettime() 
{
    return ( (double)PAPI_get_virt_usec() * 1000000.0);
}

void papi_add_counter(
    int e
){
    int retval;
    assert(eventset!=PAPI_NULL);
    assert(isInited);
    retval = PAPI_add_event(eventset, e);
    if(retval != PAPI_OK){
        fprintf(stderr, "Ignoring unknown event. \n");
    }
    
    PAPI_event_code_to_name(e, events[ev_count]);
    ev_count++;
}


void papi_add_default_counters(
    void
){
    assert(isInited);

    /* compute */
    papi_add_counter(PAPI_TOT_INS); // Total Instruction
    papi_add_counter(PAPI_TOT_CYC); // Total Cycle Count
    papi_add_counter(PAPI_FP_OPS); // Total Floating OP Counter

    /* data access */
    papi_add_counter(PAPI_L3_TCM); // L3 Memory total cache miss

    use_default = 1;
}

void measurement_start(
    void
){
    assert(ev_count>0);
    assert(isInited);

    if(PAPI_start(eventset) != PAPI_OK){
        fprintf(stderr, "cannot start PAPI counters\n");
    }
    if(values != NULL){
        values = (long long*) realloc (values, ev_count*sizeof(long long));
    }else{
        values = (long long*) calloc (ev_count, sizeof(long long));        
    }
    running = 1;
}

void measurement_stop(
    void
){
    assert(running);
    if(PAPI_stop(eventset, values) != PAPI_OK){
        fprintf(stderr, "cannot stop PAPI counters\n");
    }
    running = 0;
}

void get_counters(
    int* count, 
    counter_kvp_t* data
){
    assert(count);
    assert(data);

    for(int i=0; i<ev_count; i++){
        strncpy(data[i].name, events[i], MAX_PERF_NAME_LENGTH);
        data[i].value = values[i];
    }
}


void prof_detach(
    void
){
    measurement_stop();
    PAPI_shutdown();

    running = 0;
    isInited = 0;
    
}

void prof_reset(
    void
){
    measurement_stop();
    measurement_start();
}

void prof_get_def(
    long long* total_instructions,
    long long* total_cycle,
    long long* total_flops,
    long long* l3_cache_miss
){
    if(running){
        if(use_default){
            *total_instructions = values[0];
            *total_cycle = values[1];
            *total_flops = values[2];
            *l3_cache_miss = values[3];
        }
    }
}

int prof_peek_num_counters(
    void
){
    return ev_count;
}

void prof_start(
    void
){
    measurement_start();
}

void prof_stop(
    void
){
    measurement_stop();
}

void prof_add_counter(
    int id
){
    papi_add_counter(id);
}

void prof_get_all_counters(
    int* n_counters, 
    counter_kvp_t* counters
){
    assert(n_counters);
    assert(counters);

    if(running){
        get_counters(n_counters, counters);
    }else{
        *n_counters = 0;
    }


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

    me->gettime = papi_gettime;
    me->start = prof_start;
    me->end= prof_stop;
    me->read_all = prof_get_all_counters;
    me->peek = prof_peek_num_counters;
    me->add_c = prof_add_counter;
    me->read_def = prof_get_def;

    return (Laik_Agent*) me;

}

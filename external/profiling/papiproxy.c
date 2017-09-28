#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "papi.h"
#include "interface/agent.h"

static int eventset = PAPI_NULL;
static int isInited = 0;
static int ev_count = 0;
static int running = 0;
static long long* values = NULL;
static char events[MAX_PERF_COUNTERS][PAPI_MAX_STR_LEN];

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

float gettime() 
{
    return ( (float)PAPI_get_virt_usec() * 1000000.0);
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
    papi_add_counter(PAPI_TOT_INS);
    papi_add_counter(PAPI_TOT_CYC);
    papi_add_counter(PAPI_FP_OPS);

    /* data access */
    papi_add_counter(PAPI_L3_TCM);   
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



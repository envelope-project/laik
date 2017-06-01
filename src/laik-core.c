/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

// default log level
static Laik_LogLevel laik_loglevel = LAIK_LL_Error;
static Laik_Instance* laik_loginst = 0;

int laik_size(Laik_Group* g)
{
    return g->size;
}

int laik_myid(Laik_Group* g)
{
    return g->myid;
}

void laik_finalize(Laik_Instance* i)
{
    assert(i);
    if (i->backend && i->backend->finalize)
        (*i->backend->finalize)(i);
}

// return a backend-dependant string for the location of the calling task
char* laik_mylocation(Laik_Instance* inst)
{
    return inst->mylocation;
}

// allocate space for a new LAIK instance
Laik_Instance* laik_new_instance(Laik_Backend* b,
                                 int size, int myid,
                                 char* location, void* data)
{
    Laik_Instance* instance;
    instance = (Laik_Instance*) malloc(sizeof(Laik_Instance));

    instance->backend = b;
    instance->backend_data = data;
    instance->size = size;
    instance->myid = myid;
    instance->mylocation = strdup(location);

    instance->firstspace = 0;

    instance->group_count = 0;
    instance->data_count = 0;
    instance->mapping_count = 0;

    laik_data_init();

    // logging (TODO: multiple instances)
    laik_loginst = instance;
    char* str = getenv("LAIK_LOG");
    if (str) {
        int l = atoi(str);
        if (l > 0)
            laik_loglevel = l;
    }
    //TEMPORARY
    //laik_loglevel = 1;
    return instance;
}

// create a group to be used in this LAIK instance
Laik_Group* laik_create_group(Laik_Instance* i)
{
    assert(i->group_count < MAX_GROUPS);

    Laik_Group* g;

    g = (Laik_Group*) malloc(sizeof(Laik_Group) + (i->size) * sizeof(int));
    i->group[i->group_count] = g;

    g->inst = i;
    g->gid = i->group_count;
    g->size = 0; // yet invalid

    i->group_count++;

    return g;
}

Laik_Group* laik_world(Laik_Instance* i)
{
    // world must have been added by backend
    assert(i->group_count > 0);

    Laik_Group* g = i->group[0];
    assert(g->gid == 0);
    assert(g->inst == i);
    assert(g->size == i->size);

    return g;
}

// Logging

// to overwrite environment variable LAIK_LOG
void laik_set_loglevel(Laik_LogLevel l)
{
    laik_loglevel = l;
}

// check for log level: return true if given log level will be shown
bool laik_logshown(Laik_LogLevel l)
{
    return (l >= laik_loglevel);
}

// log a message, similar to printf
void laik_log(Laik_LogLevel l, char* msg, ...)
{
    if (l < laik_loglevel) return;

    assert(laik_loginst != 0);
    const char* lstr = "";
    switch(l) {
        case LAIK_LL_Warning: lstr = "Warning "; break;
        case LAIK_LL_Error:   lstr = "ERROR "; break;
        case LAIK_LL_Panic:   lstr = "PANIC "; break;
        default: break;
    }

    // print at once to not mix output from multiple (MPI) tasks
    char format[1000];
    sprintf(format, "LAIK %d/%d%s - %s",
            laik_loginst->myid, laik_loginst->size, lstr, msg);

    va_list args;
    va_start(args, msg);
    vfprintf(stderr, format, args);

    // terminate program on panic
    if (l == LAIK_LL_Panic) exit(1);
}

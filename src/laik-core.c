/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>


int laik_size(Laik_Instance* i)
{
    return i->size;
}

int laik_myid(Laik_Instance* i)
{
    return i->myid;
}

void laik_finalize(Laik_Instance* i)
{
    assert(i);
    if (i->backend && i->backend->finalize)
        (*i->backend->finalize)(i);
}


// allocate space for a new LAIK instance
Laik_Instance* laik_new_instance(Laik_Backend* b)
{
    Laik_Instance* instance = (Laik_Instance*) malloc(sizeof(Laik_Instance));

    instance->backend = b;
    instance->size = 0; // invalid
    instance->myid = 0;

    instance->firstspace = 0;

    instance->group_count = 0;
    instance->data_count = 0;
    instance->mapping_count = 0;

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
    g->count = 0; // yet invalid

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
    assert(g->count == i->size);

    return g;
}

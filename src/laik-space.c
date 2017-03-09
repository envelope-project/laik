/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>

// create a new index space object (initially invalid)
Laik_Space* laik_new_space(Laik_Instance* i)
{
    Laik_Space* space = (Laik_Space*) malloc(sizeof(Laik_Space));

    space->inst = i;
    space->dims = 0; // invalid
    space->first = 0;

    // append this space to list of spaces used by LAIK instance
    space->next = i->firstspace;
    i->firstspace = space;

    return space;
}

// create a new index space object with an initial size
Laik_Space* laik_new_space_1d(Laik_Instance* i, int s1)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 1;
    space->size[0] = s1;

    return space;
}

Laik_Space* laik_new_space_2d(Laik_Instance* i, int s1, int s2)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 2;
    space->size[0] = s1;
    space->size[1] = s2;

    return space;
}

Laik_Space* laik_new_space_3d(Laik_Instance* i, int s1, int s2, int s3)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 3;
    space->size[0] = s1;
    space->size[1] = s2;
    space->size[2] = s3;

    return space;
}


// change the size of an index space, eventually triggering a repartitiong
void laik_change_space_1d(Laik_Space* s, int s1)
{
    assert(s->dims == 1);
    if (s->size[0] == s1) return;

    s->size[0] = s1;

    // TODO: notify partitionings about space change
}

void laik_change_space_2d(Laik_Space* s, int s1, int s2)
{
    assert(0); // TODO
}

void laik_change_space_3d(Laik_Space* s, int s1, int s2, int s3)
{
    assert(0); // TODO
}


// free a space with all resources depending on it (e.g. paritionings)
void laik_free_space(Laik_Space* s)
{
    assert(0); // TODO
}

// create a new partitioning on a space
Laik_Partitioning*
laik_new_base_partitioning(Laik_Space* s,
                           Laik_PartitionType pt,
                           Laik_AccessPermission ap)
{
    Laik_Partitioning* partitioning;
    partitioning = (Laik_Partitioning*) malloc(sizeof(Laik_Partitioning));

    partitioning->space = s;
    partitioning->permission = ap;
    partitioning->type = pt;
    partitioning->group = laik_world(s->inst);

    partitioning->base = 0;
    partitioning->next = 0;
    partitioning->coupledDimFrom = 0;
    partitioning->coupledDimTo = 0;
    partitioning->haloWidth = 0;

    partitioning->bordersValid = false;

    return partitioning;
}

// create a new partitioning based on another one on the same space
Laik_Partitioning*
laik_new_coupled_partitioning(Laik_Partitioning* p,
                              Laik_PartitionType pt,
                              Laik_AccessPermission ap)
{
    Laik_Partitioning* partitioning;
    partitioning = laik_new_base_partitioning(p->space, pt, ap);

    assert(0); // TODO

    return partitioning;
}

// create a new partitioning based on another one on a different space
// this also needs to know which dimensions should be coupled
Laik_Partitioning*
laik_new_spacecoupled_partitioning(Laik_Partitioning* p,
                                   Laik_Space* s, int from, int to,
                                   Laik_PartitionType pt,
                                   Laik_AccessPermission ap)
{
    Laik_Partitioning* partitioning;
    partitioning = laik_new_base_partitioning(p->space, pt, ap);

    assert(0); // TODO

    return partitioning;
}

// append a partitioning to a partioning group whose consistency should
// be enforced at the same point in time
void laik_append_partitioning(Laik_PartGroup* g, Laik_Partitioning* p)
{
    assert(0); // TODO
}

// Calculate communication required for transitioning between partitionings
Laik_PartTransition* laik_calc_transition(Laik_PartGroup* from,
                                          Laik_PartGroup* to)
{
    assert(0); // TODO
}

// enforce consistency for the partitioning group, depending on previous
void laik_enforce_consistency(Laik_Instance* i, Laik_PartGroup* g)
{
    assert(0); // TODO
}

// set a weight for each participating task in a partitioning, to be
//  used when a repartitioning is requested
void laik_set_partition_weights(Laik_Partitioning* p, int* w)
{
    assert(0); // TODO
}


// change an existing base partitioning
void laik_repartition(Laik_Partitioning* p, Laik_PartitionType pt)
{
    assert(0); // TODO
}


// couple different LAIK instances via spaces:
// one partition of calling task in outer space is mapped to inner space
void laik_couple_nested(Laik_Space* outer, Laik_Space* inner)
{
    assert(0); // TODO
}




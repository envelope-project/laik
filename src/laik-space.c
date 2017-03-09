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
    space->first_partitioning = 0;

    // append this space to list of spaces used by LAIK instance
    space->next = i->firstspace;
    i->firstspace = space;

    return space;
}

// create a new index space object with an initial size
Laik_Space* laik_new_space_1d(Laik_Instance* i, uint64_t s1)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 1;
    space->size[0] = s1;

    return space;
}

Laik_Space* laik_new_space_2d(Laik_Instance* i,
                              uint64_t s1, uint64_t s2)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 2;
    space->size[0] = s1;
    space->size[1] = s2;

    return space;
}

Laik_Space* laik_new_space_3d(Laik_Instance* i,
                              uint64_t s1, uint64_t s2, uint64_t s3)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 3;
    space->size[0] = s1;
    space->size[1] = s2;
    space->size[2] = s3;

    return space;
}


// change the size of an index space, eventually triggering a repartitiong
void laik_change_space_1d(Laik_Space* s, uint64_t s1)
{
    assert(s->dims == 1);
    if (s->size[0] == s1) return;

    s->size[0] = s1;

    // TODO: notify partitionings about space change
}

void laik_change_space_2d(Laik_Space* s,
                          uint64_t s1, uint64_t s2)
{
    assert(0); // TODO
}

void laik_change_space_3d(Laik_Space* s,
                          uint64_t s1, uint64_t s2, uint64_t s3)
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
    Laik_Partitioning* p;
    p = (Laik_Partitioning*) malloc(sizeof(Laik_Partitioning));

    p->space = s;
    p->next = s->first_partitioning;
    s->first_partitioning = p;

    p->permission = ap;
    p->type = pt;
    p->group = laik_world(s->inst);
    p->pdim = 0;

    p->base = 0;
    p->haloWidth = 0;

    p->bordersValid = false;
    p->borders = 0;

    return p;
}

// for multiple-dimensional spaces, set dimension to partition (default is 0)
void laik_set_partitioning_dimension(Laik_Partitioning* p, int d)
{
    assert((d >= 0) && (d < p->space->dims));
    p->pdim = d;
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

// make sure partitioning borders are up to date
void laik_update_partitioning(Laik_Partitioning* p)
{
    Laik_Slice* baseBorders = 0;
    int pdim = p->pdim;
    int basepdim;

    if (p->base) {
        laik_update_partitioning(p->base);
        baseBorders = p->base->borders;
        basepdim = p->base->pdim;
        // sizes of coupled dimensions should be equal
        assert(p->space->size[pdim] == p->base->space->size[basepdim]);
    }

    if (p->bordersValid) return;

    int count = p->group->count;
    if (!p->borders)
        p->borders = (Laik_Slice*) malloc(count * sizeof(Laik_Slice));

    // partition according to dimension 0
    uint64_t size = p->space->size[pdim];
    uint64_t idx = 0;
    uint64_t inc = (size / count)+1;

    for(int task = 0; task < count; task++) {
        Laik_Slice* b = &(p->borders[task]);
        switch(p->type) {
        case LAIK_PT_All:
            b->from.i[0] = 0;
            b->from.i[1] = 0;
            b->from.i[2] = 0;
            b->to.i[0] = p->space->size[0];
            b->to.i[1] = p->space->size[1];
            b->to.i[2] = p->space->size[2];
            break;

        case LAIK_PT_Stripe:
            b->from.i[0] = 0;
            b->from.i[1] = 0;
            b->from.i[2] = 0;
            b->to.i[0] = p->space->size[0];
            b->to.i[1] = p->space->size[1];
            b->to.i[2] = p->space->size[2];

            b->from.i[pdim] = idx;
            idx += inc;
            if (idx > size) idx = size;
            b->to.i[pdim] = idx;
            break;

        case LAIK_PT_Master:
            b->from.i[0] = 0;
            b->from.i[1] = 0;
            b->from.i[2] = 0;
            b->to.i[0] = (task == 0) ? p->space->size[0] : 0;
            b->to.i[1] = (task == 0) ? p->space->size[1] : 0;
            b->to.i[2] = (task == 0) ? p->space->size[2] : 0;
            break;

        case LAIK_PT_Copy:
            assert(baseBorders);
            b->from.i[0] = 0;
            b->from.i[1] = 0;
            b->from.i[2] = 0;
            b->to.i[0] = p->space->size[0];
            b->to.i[1] = p->space->size[1];
            b->to.i[2] = p->space->size[2];

            b->from.i[pdim] = baseBorders[task].from.i[basepdim];
            b->to.i[pdim] = baseBorders[task].to.i[basepdim];
            break;

        default:
            assert(0); // TODO
            break;
        }
    }
    p->bordersValid = true;
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




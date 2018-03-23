/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

// counter for space ID, just for debugging
static int space_id = 0;

// counter for partitioning ID, just for debugging
static int aphase_id = 0;

// helpers

void laik_set_index(Laik_Index* i, int64_t i1, int64_t i2, int64_t i3)
{
    i->i[0] = i1;
    i->i[1] = i2;
    i->i[2] = i3;
}

void laik_add_index(Laik_Index* res, Laik_Index* src1, Laik_Index* src2)
{
    res->i[0] = src1->i[0] + src2->i[0];
    res->i[1] = src1->i[1] + src2->i[1];
    res->i[2] = src1->i[2] + src2->i[2];
}

void laik_sub_index(Laik_Index* res, const Laik_Index* src1, const Laik_Index* src2)
{
    res->i[0] = src1->i[0] - src2->i[0];
    res->i[1] = src1->i[1] - src2->i[1];
    res->i[2] = src1->i[2] - src2->i[2];
}


bool laik_index_isEqual(int dims, const Laik_Index* i1, const Laik_Index* i2)
{
    if (i1->i[0] != i2->i[0]) return false;
    if (dims == 1) return true;

    if (i1->i[1] != i2->i[1]) return false;
    if (dims == 2) return true;

    if (i1->i[2] != i2->i[2]) return false;
    return true;
}

// is the given slice empty?
bool laik_slice_isEmpty(int dims, Laik_Slice* slc)
{
    if (slc->from.i[0] >= slc->to.i[0])
        return true;

    if (dims>1) {
        if (slc->from.i[1] >= slc->to.i[1])
            return true;

        if (dims>2) {
            if (slc->from.i[2] >= slc->to.i[2])
                return true;
        }
    }
    return false;
}


// returns false if intersection of ranges is empty
static
bool intersectRange(int64_t from1, int64_t to1, int64_t from2, int64_t to2,
                    int64_t* resFrom, int64_t* resTo)
{
    if (from1 >= to2) return false;
    if (from2 >= to1) return false;
    *resFrom = (from1 > from2) ? from1 : from2;
    *resTo = (to1 > to2) ? to2 : to1;
    return true;
}

// get the intersection of 2 slices; return 0 if intersection is empty
Laik_Slice* laik_slice_intersect(int dims,
                                 const Laik_Slice* s1, const Laik_Slice* s2)
{
    static Laik_Slice s;

    if (!intersectRange(s1->from.i[0], s1->to.i[0],
                        s2->from.i[0], s2->to.i[0],
                        &(s.from.i[0]), &(s.to.i[0])) ) return 0;
    if (dims>1) {
        if (!intersectRange(s1->from.i[1], s1->to.i[1],
                            s2->from.i[1], s2->to.i[1],
                            &(s.from.i[1]), &(s.to.i[1])) ) return 0;
        if (dims>2) {
            if (!intersectRange(s1->from.i[2], s1->to.i[2],
                                s2->from.i[2], s2->to.i[2],
                                &(s.from.i[2]), &(s.to.i[2])) ) return 0;
        }
    }
    return &s;
}

// expand slice <dst> such that it contains <src>
void laik_slice_expand(int dims, Laik_Slice* dst, Laik_Slice* src)
{
    if (src->from.i[0] < dst->from.i[0]) dst->from.i[0] = src->from.i[0];
    if (src->to.i[0] > dst->to.i[0]) dst->to.i[0] = src->to.i[0];
    if (dims == 1) return;

    if (src->from.i[1] < dst->from.i[1]) dst->from.i[1] = src->from.i[1];
    if (src->to.i[1] > dst->to.i[1]) dst->to.i[1] = src->to.i[1];
    if (dims == 2) return;

    if (src->from.i[2] < dst->from.i[2]) dst->from.i[2] = src->from.i[2];
    if (src->to.i[2] > dst->to.i[2]) dst->to.i[2] = src->to.i[2];
}

// is slice <slc1> contained in <slc2>?
bool laik_slice_within_slice(int dims, const Laik_Slice* slc1, const Laik_Slice* slc2)
{
    if (slc1->from.i[0] < slc1->to.i[0]) {
        // not empty
        if (slc1->from.i[0] < slc2->from.i[0]) return false;
        if (slc1->to.i[0] > slc2->to.i[0]) return false;
    }
    if (dims == 1) return true;

    if (slc1->from.i[1] < slc1->to.i[1]) {
        // not empty
        if (slc1->from.i[1] < slc2->from.i[1]) return false;
        if (slc1->to.i[1] > slc2->to.i[1]) return false;
    }
    if (dims == 2) return true;

    if (slc1->from.i[2] < slc1->to.i[2]) {
        // not empty
        if (slc1->from.i[2] < slc2->from.i[2]) return false;
        if (slc1->to.i[2] > slc2->to.i[2]) return false;
    }
    return true;
}

// is slice within space borders?
bool laik_slice_within_space(const Laik_Slice* slc, const Laik_Space* sp)
{
    return laik_slice_within_slice(sp->dims, slc, &(sp->s));
}

// are the slices equal?
bool laik_slice_isEqual(int dims, Laik_Slice* s1, Laik_Slice* s2)
{
    if (!laik_index_isEqual(dims, &(s1->from), &(s2->from))) return false;
    if (!laik_index_isEqual(dims, &(s1->to), &(s2->to))) return false;
    return true;
}


// number of indexes in the slice
uint64_t laik_slice_size(int dims, const Laik_Slice* s)
{
    uint64_t size = s->to.i[0] - s->from.i[0];
    if (dims > 1) {
        size *= s->to.i[1] - s->from.i[1];
        if (dims > 2)
            size *= s->to.i[2] - s->from.i[2];
    }
    return size;
}

// get the index slice covered by the space
const Laik_Slice* laik_space_asslice(Laik_Space* space)
{
    return &(space->s);
}

// get the number of dimensions if this is a regular space
int laik_space_getdimensions(Laik_Space* space)
{
    return space->dims;
}


// is this a reduction?
bool laik_is_reduction(Laik_DataFlow flow)
{
    if (flow & LAIK_DF_ReduceOut)
        return true;
    return false;
}

// return the reduction operation from data flow behavior
Laik_ReductionOperation laik_get_reduction(Laik_DataFlow flow)
{
    if (flow & LAIK_DF_Sum)
        return LAIK_RO_Sum;
    return LAIK_RO_None;
}

// do we need to copy values in?
bool laik_do_copyin(Laik_DataFlow flow)
{
    if (flow & LAIK_DF_CopyIn)
        return true;
    return false;
}

// do we need to copy values out?
bool laik_do_copyout(Laik_DataFlow flow)
{
    if (flow & LAIK_DF_CopyOut)
        return true;
    return false;
}

// do we need to init values?
bool laik_do_init(Laik_DataFlow flow)
{
    if (flow & LAIK_DF_Init)
        return true;
    return false;
}


//-----------------------
// Laik_Space

// create a new index space object (initially invalid)
Laik_Space* laik_new_space(Laik_Instance* inst)
{
    Laik_Space* space = malloc(sizeof(Laik_Space));
    if (!space) {
        laik_panic("Out of memory allocating Laik_Space object");
        exit(1); // not actually needed, laik_panic never returns
    }

    space->id = space_id++;
    space->name = strdup("space-0     ");
    sprintf(space->name, "space-%d", space->id);

    space->inst = inst;
    space->dims = 0; // invalid
    space->firstAccessPhaseForSpace = 0;
    space->nextSpaceForInstance = 0;

    // append this space to list of spaces used by LAIK instance
    laik_addSpaceForInstance(inst, space);

    return space;
}

// create a new index space object with an initial size
Laik_Space* laik_new_space_1d(Laik_Instance* i, int64_t s1)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 1;
    space->s.from.i[0] = 0;
    space->s.to.i[0] = s1;

    if (laik_log_begin(1)) {
        laik_log_append("new 1d space '%s': ", space->name);
        laik_log_Space(space);
        laik_log_flush(0);
    }

    return space;
}

Laik_Space* laik_new_space_2d(Laik_Instance* i, int64_t s1, int64_t s2)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 2;
    space->s.from.i[0] = 0;
    space->s.to.i[0] = s1;
    space->s.from.i[1] = 0;
    space->s.to.i[1] = s2;

    if (laik_log_begin(1)) {
        laik_log_append("new 2d space '%s': ", space->name);
        laik_log_Space(space);
        laik_log_flush(0);
    }

    return space;
}

Laik_Space* laik_new_space_3d(Laik_Instance* i,
                              int64_t s1, int64_t s2, int64_t s3)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 3;
    space->s.from.i[0] = 0;
    space->s.to.i[0] = s1;
    space->s.from.i[1] = 0;
    space->s.to.i[1] = s2;
    space->s.from.i[2] = 0;
    space->s.to.i[2] = s3;

    if (laik_log_begin(1)) {
        laik_log_append("new 3d space '%s': ", space->name);
        laik_log_Space(space);
        laik_log_flush(0);
    }

    return space;
}

// free a space with all resources depending on it (e.g. paritionings)
void laik_free_space(Laik_Space* s)
{
    free(s->name);
    laik_removeSpaceFromInstance(s->inst, s);
    // TODO
}

uint64_t laik_space_size(Laik_Space* s)
{
    return laik_slice_size(s->dims, &(s->s));
}


// set a space a name, for debug output
void laik_set_space_name(Laik_Space* s, char* n)
{
    s->name = strdup(n);
}

// change the size of an index space, eventually triggering a repartitiong
void laik_change_space_1d(Laik_Space* s, int64_t from1, int64_t to1)
{
    assert(s->dims == 1);
    if ((s->s.from.i[0] == from1) && (s->s.to.i[0] == to1))
        return;

    s->s.from.i[0] = from1;
    s->s.to.i[0] = to1;

    // TODO: notify partitionings about space change
}



void laik_addAccessPhaseForSpace(Laik_Space* s, Laik_AccessPhase* p)
{
    assert(p->nextAccessPhaseForSpace == 0);
    p->nextAccessPhaseForSpace = s->firstAccessPhaseForSpace;
    s->firstAccessPhaseForSpace = p;
}

void laik_removeAccessPhaseForSpace(Laik_Space* s, Laik_AccessPhase* p)
{
    if (s->firstAccessPhaseForSpace == p) {
        s->firstAccessPhaseForSpace = p->nextAccessPhaseForSpace;
    }
    else {
        // search for previous item
        Laik_AccessPhase* pp = s->firstAccessPhaseForSpace;
        while(pp->nextAccessPhaseForSpace != p)
            pp = pp->nextAccessPhaseForSpace;
        assert(pp != 0); // not found, should not happen
        pp->nextAccessPhaseForSpace = p->nextAccessPhaseForSpace;
    }
    p->nextAccessPhaseForSpace = 0;
}






//-----------------------
// Laik_AccessPhase


// create a new access phase on a space
Laik_AccessPhase*
laik_new_accessphase(Laik_Group* group, Laik_Space* space,
                     Laik_Partitioner* pr, Laik_AccessPhase *base)
{
    Laik_AccessPhase* ap;
    ap = malloc(sizeof(Laik_AccessPhase));
    if (!ap) {
        laik_panic("Out of memory allocating Laik_AccessPhase object");
        exit(1); // not actually needed, laik_panic never returns
    }

    ap->id = aphase_id++;
    ap->name = strdup("phase-0     ");
    sprintf(ap->name, "phase-%d", ap->id);

    assert(group->inst == space->inst);
    ap->group = group;
    ap->space = space;

    ap->partitioner = pr;
    ap->base = base;

    ap->hasValidPartitioning = false;
    ap->partitioning = 0;

    ap->nextAccessPhaseForSpace = 0;
    ap->nextAccessPhaseForGroup = 0;
    ap->nextAccessPhaseForBase  = 0;
    ap->firstDataForAccessPhase = 0;
    ap->firstAccessPhaseForBase = 0;

    laik_addAccessPhaseForSpace(space, ap);
    laik_addAcessPhaseForGroup(ap->group, ap);

    if (base) {
        assert(base->group == group);
        laik_addAccessPhaseForBase(base, ap);
    }

    if (laik_log_begin(1)) {
        laik_log_append("new access phase '%s':\n  space '%s', "
                        "group %d (size %d, myid %d), partitioner '%s'",
                        ap->name, space->name,
                        ap->group->gid, ap->group->size, ap->group->myid,
                        pr->name);
        if (base)
            laik_log_append(", base '%s'", base->name);
        laik_log_flush(0);
    }

    return ap;
}

void laik_addAccessPhaseForBase(Laik_AccessPhase* base,
                                Laik_AccessPhase* ap)
{
    assert(ap->nextAccessPhaseForBase == 0);
    ap->nextAccessPhaseForBase = base->firstAccessPhaseForBase;
    base->firstAccessPhaseForBase = ap;
}

void laik_removeAccessPhaseForBase(Laik_AccessPhase* base,
                                   Laik_AccessPhase* ap)
{
    if (base->firstAccessPhaseForBase == ap) {
        base->firstAccessPhaseForBase = ap->nextAccessPhaseForBase;
    }
    else {
        // search for previous item
        Laik_AccessPhase* pp = base->firstAccessPhaseForBase;
        while(pp->nextAccessPhaseForBase != ap)
            pp = pp->nextAccessPhaseForBase;
        assert(pp != 0); // not found, should not happen
        pp->nextAccessPhaseForBase = ap->nextAccessPhaseForBase;
    }
    ap->nextAccessPhaseForBase = 0;
}


void laik_addDataForAccessPhase(Laik_AccessPhase* ap, Laik_Data* d)
{
    assert(d->nextAccessPhaseUser == 0);
    d->nextAccessPhaseUser = ap->firstDataForAccessPhase;
    ap->firstDataForAccessPhase = d;
}

void laik_removeDataFromAccessPhase(Laik_AccessPhase* ap, Laik_Data* d)
{
    if (ap->firstDataForAccessPhase == d) {
        ap->firstDataForAccessPhase = d->nextAccessPhaseUser;
    }
    else {
        // search for previous item
        Laik_Data* dd = ap->firstDataForAccessPhase;
        while(dd->nextAccessPhaseUser != d)
            dd = dd->nextAccessPhaseUser;
        assert(dd != 0); // not found, should not happen
        dd->nextAccessPhaseUser = d->nextAccessPhaseUser;
    }
    d->nextAccessPhaseUser = 0;
}


void laik_set_partitioner(Laik_AccessPhase* ap, Laik_Partitioner* pr)
{
    assert(pr->run != 0);
    ap->partitioner = pr;
}

Laik_Partitioner* laik_get_partitioner(Laik_AccessPhase* ap)
{
    return ap->partitioner;
}

Laik_Space* laik_get_apspace(Laik_AccessPhase* ap)
{
    return ap->space;
}

Laik_Group* laik_get_apgroup(Laik_AccessPhase* ap)
{
    return ap->group;
}


// free a partitioning with related resources
void laik_free_accessphase(Laik_AccessPhase* ap)
{
    // FIXME: needs some kind of reference counting
    return;
    if (ap->firstDataForAccessPhase == 0) {
        laik_removeAccessPhaseForGroup(ap->group, ap);
        laik_removeAccessPhaseForSpace(ap->space, ap);
        if (ap->base)
            laik_removeAccessPhaseForBase(ap->base, ap);
        free(ap->name);
        free(ap->partitioning);
    }
}

// get number of slices of this task
int laik_phase_my_slicecount(Laik_AccessPhase* ap)
{
    if (!ap->hasValidPartitioning)
        laik_phase_run_partitioner(ap);

    return laik_my_slicecount(ap->partitioning);
}

// get number of mappings of this task
int laik_phase_my_mapcount(Laik_AccessPhase* ap)
{
    if (!ap->hasValidPartitioning)
        laik_phase_run_partitioner(ap);

    return laik_my_mapcount(ap->partitioning);
}

int laik_phase_my_mapslicecount(Laik_AccessPhase* ap, int mapNo)
{
    if (!ap->hasValidPartitioning)
        laik_phase_run_partitioner(ap);

    return laik_my_mapslicecount(ap->partitioning, mapNo);
}

int laik_tslice_get_mapNo(Laik_TaskSlice* ts)
{
    switch(ts->type) {
    case TS_Generic: {
        Laik_TaskSlice_Gen* tsg = (Laik_TaskSlice_Gen*) ts;
        return tsg->mapNo;
    }
    case TS_Single1d:
        return 0;
    default:
        assert(0);
    }
}

Laik_Slice* laik_tslice_get_slice(Laik_TaskSlice* ts)
{
    switch(ts->type) {
    case TS_Generic: {
        Laik_TaskSlice_Gen* tsg = (Laik_TaskSlice_Gen*) ts;
        return &(tsg->s);
    }
    default:
        assert(0);
    }
}

// get slice number <n> from the slices of this task
Laik_TaskSlice* laik_phase_my_slice(Laik_AccessPhase* ap, int n)
{
    if (!ap->hasValidPartitioning)
        laik_phase_run_partitioner(ap);

    return laik_my_slice(ap->partitioning, n);
}

Laik_TaskSlice* laik_phase_my_mapslice(Laik_AccessPhase* ap, int mapNo, int n)
{
    if (!ap->hasValidPartitioning)
        laik_phase_run_partitioner(ap);

    return laik_my_mapslice(ap->partitioning, mapNo, n);
}

Laik_TaskSlice* laik_phase_myslice_1d(Laik_AccessPhase* ap, int n,
                                      int64_t* from, int64_t* to)
{
    if (!ap->hasValidPartitioning)
        laik_phase_run_partitioner(ap);

    return laik_my_slice_1d(ap->partitioning, n, from, to);
}

Laik_TaskSlice* laik_phase_myslice_2d(Laik_AccessPhase* ap, int n,
                                      int64_t* x1, int64_t* x2,
                                      int64_t* y1, int64_t* y2)
{
    if (!ap->hasValidPartitioning)
        laik_phase_run_partitioner(ap);

    return laik_my_slice_2d(ap->partitioning, n, x1, x2, y1, y2);
}

Laik_TaskSlice* laik_phase_myslice_3d(Laik_AccessPhase* ap, int n,
                                      int64_t* x1, int64_t* x2,
                                      int64_t* y1, int64_t* y2,
                                      int64_t* z1, int64_t* z2)
{
    if (!ap->hasValidPartitioning)
        laik_phase_run_partitioner(ap);

    return laik_my_slice_3d(ap->partitioning, n, x1, x2, y1, y2, z1, z2);
}


// applications can attach arbitrary values to a TaskSlice, to be
// passed from application-specific partitioners to slice processing
void* laik_get_slice_data(Laik_TaskSlice* ts)
{
    assert(ts->type == TS_Generic);
    Laik_TaskSlice_Gen* tsg = (Laik_TaskSlice_Gen*) ts;
    return tsg->data;
}

void laik_set_slice_data(Laik_TaskSlice* ts, void* data)
{
    assert(ts->type == TS_Generic);
    Laik_TaskSlice_Gen* tsg = (Laik_TaskSlice_Gen*) ts;
    tsg->data = data;
}


// give an access phase a name, for debug output
void laik_set_accessphase_name(Laik_AccessPhase* ap, char* n)
{
    ap->name = strdup(n);
}





// set new partitioning borders
void laik_phase_set_partitioning(Laik_AccessPhase* ap, Laik_Partitioning* p)
{
    assert(ap->group == p->group);
    assert(ap->space == p->space);

    if (laik_log_begin(1)) {
        laik_log_append("setting partitioning for access phase '%s' (group %d, myid %d):\n  ",
                        ap->name, p->group->gid, p->group->myid);
        laik_log_Partitioning(p);
        laik_log_flush(0);
    }

    if (ap->hasValidPartitioning &&
        laik_partitioning_isEqual(ap->partitioning, p)) {
        laik_log(1, "partitioning equal to original, nothing to do");
        return;
    }

    // visit all users of this partitioning:
    // first, all partitionings coupled to this as base
    Laik_AccessPhase* apdep = ap->firstAccessPhaseForBase;
    while(apdep) {
        assert(apdep->base == ap);
        assert(apdep->partitioner);
        Laik_Partitioning* pdep;
        pdep = laik_new_partitioning(apdep->partitioner,
                                    apdep->group, apdep->space, p);

        laik_phase_set_partitioning(apdep, pdep);
        apdep = apdep->nextAccessPhaseForBase;
    }
    // second, all data containers using this partitioning
    Laik_Data* d = ap->firstDataForAccessPhase;
    while(d) {
        laik_switchto_partitioning(d, p, LAIK_DF_Previous);
        d = d->nextAccessPhaseUser;
    }

    if (ap->partitioning)
        laik_free_partitioning(ap->partitioning);
    ap->partitioning = p;
    ap->hasValidPartitioning = true;
}

// Return currently used partitioning of given access phase.
//
// Return 0 if no partitioning is calculated yet (even if partitioner is set).
Laik_Partitioning* laik_phase_get_partitioning(Laik_AccessPhase* p)
{
    if (p->hasValidPartitioning) {
        assert(p->partitioning);
        return p->partitioning;
    }
    return 0;
}

// Force re-run of the configured partitioner for given access phase.
//
// If the partitioning is configured to depend on the partitioning of
// another access phase (ie. is derived from it), this base partitioning
// must exist.
// If you want to also trigger a new calculation of the base partitioning,
// call this function for the base access phase, which automatically will
// trigger a re-run for every derived partitioning.
Laik_Partitioning* laik_phase_run_partitioner(Laik_AccessPhase* ap)
{
    Laik_Partitioning* p;

    if (ap->base) {
        assert(ap->base->hasValidPartitioning);
    }

    assert(ap->partitioner);
    p = laik_new_partitioning(ap->partitioner,
                             ap->group, ap->space,
                             ap->base ? ap->base->partitioning : 0);

    laik_phase_set_partitioning(ap, p);

    return p;
}


// get local index from global one. return false if not local
bool laik_index_global2local(Laik_Partitioning* p,
                             Laik_Index* global, Laik_Index* local)
{
    (void) p;     /* FIXME: Why have this parameter if it's never used */
    (void) global; /* FIXME: Why have this parameter if it's never used */
    (void) local;  /* FIXME: Why have this parameter if it's never used */

    // TODO

    return true;
}


// append a partitioning to a partioning group whose consistency should
// be enforced at the same point in time
void laik_append_phase(Laik_PartGroup* g, Laik_AccessPhase* ap)
{
    (void) g; /* FIXME: Why have this parameter if it's never used */
    (void) ap; /* FIXME: Why have this parameter if it's never used */

    assert(0); // TODO
}


//-------------------------------------------------------------------------
// Laik_Transition
//


// helper functions for laik_calc_transition

// TODO:
// - quadratic complexity for 2d/3d spaces
// - for 1d, does not cope with overlapping slices belonging to same task

// print verbose debug output for creating slices for reductions?
#define DEBUG_REDUCTIONSLICES 1


static TaskGroup* groupList = 0;
static int groupListSize = 0, groupListCount = 0;

static
void cleanGroupList()
{
    for(int i = 0; i < groupListCount; i++)
        free(groupList[i].task);
    groupListCount = 0;

    // we keep the groupList array
}

static
TaskGroup* newTaskGroup(int* group)
{
    if (groupListCount == groupListSize) {
        // enlarge group list
        groupListSize = (groupListSize + 10) * 2;
        groupList = realloc(groupList, groupListSize * sizeof(TaskGroup));
        if (!groupList) {
            laik_panic("Out of memory allocating memory for Laik_Transition");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    TaskGroup* g = &(groupList[groupListCount]);
    if (group) *group = groupListCount;
    groupListCount++;

    g->count = 0; // invalid
    g->task = 0;

    return g;
}

static int getTaskGroupSingle(int task)
{
    // already existing?
    for(int i = 0; i < groupListCount; i++)
        if ((groupList[i].count == 1) && (groupList[i].task[0] == task))
            return i;

    int group;
    TaskGroup* g = newTaskGroup(&group);

    g->count = 1;
    g->task = malloc(sizeof(int));
    assert(g->task);
    g->task[0] = task;

    return group;
}

// append given task group if not already in groupList, return index
static int getTaskGroup(TaskGroup* tg)
{
    // already existing?
    int i, j;
    for(i = 0; i < groupListCount; i++) {
        if (tg->count != groupList[i].count) continue;
        for(j = 0; j < tg->count; j++)
            if (tg->task[j] != groupList[i].task[j]) break;
        if (j == tg->count)
            return i; // found
    }

    int group;
    TaskGroup* g = newTaskGroup(&group);

    g->count = tg->count;
    int tsize = tg->count * sizeof(int);
    g->task = malloc(tsize);
    assert(g->task);
    memcpy(g->task, tg->task, tsize);

    return group;
}


// only for 1d
typedef struct _SliceBorder {
    int64_t b;
    int task;
    int sliceNo, mapNo;
    unsigned int isStart :1;
    unsigned int isInput :1;
} SliceBorder;

static SliceBorder* borderList = 0;
int borderListSize = 0, borderListCount = 0;

static
void cleanBorderList()
{
    borderListCount = 0;
}

static
void appendBorder(int64_t b, int task, int sliceNo, int mapNo,
                  bool isStart, bool isInput)
{
    if (borderListCount == borderListSize) {
        // enlarge list
        borderListSize = (borderListSize + 10) * 2;
        borderList = realloc(borderList, borderListSize * sizeof(SliceBorder));
        if (!borderList) {
            laik_panic("Out of memory allocating memory for Laik_Transition");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    SliceBorder *sb = &(borderList[borderListCount]);
    borderListCount++;

    sb->b = b;
    sb->task = task;
    sb->sliceNo = sliceNo;
    sb->mapNo = mapNo;
    sb->isStart = isStart ? 1 : 0;
    sb->isInput = isInput ? 1 : 0;

#ifdef DEBUG_REDUCTIONSLICES
    laik_log(1, "  add border %lld, task %d slice/map %d/%d (%s, %s)",
             (long long int) b, task, sliceNo, mapNo,
             isStart ? "start" : "end", isInput ? "input" : "output");
#endif
}

static int sb_cmp(const void *p1, const void *p2)
{
    const SliceBorder* sb1 = (const SliceBorder*) p1;
    const SliceBorder* sb2 = (const SliceBorder*) p2;
    // order by border, at same point first close slice
    if (sb1->b == sb2->b) {
        return sb1->isStart - sb2->isStart;
    }
    return sb1->b - sb2->b;

}

static bool addTask(TaskGroup* g, int task, int maxTasks)
{
    int o = 0;
    while(o < g->count) {
        if (task < g->task[o]) break;
        if (task == g->task[o]) return false; // already in group
        o++;
    }
    while(o < g->count) {
        int tmp = g->task[o];
        g->task[o] = task;
        task = tmp;
        o++;
    }
    assert(g->count < maxTasks);
    g->task[o] = task;
    g->count++;
    return true;
}

static bool removeTask(TaskGroup* g, int task)
{
    if (g->count == 0) return false;
    int o = 0;
    while(o < g->count) {
        if (task < g->task[o]) return false; // not found
        if (task == g->task[o]) break;
        o++;
    }
    o++;
    while(o < g->count) {
        g->task[o - 1] = g->task[o];
        o++;
    }
    g->count--;
    return true;
}

static bool isInTaskGroup(TaskGroup* g, int task)
{
    for(int i = 0; i< g->count; i++)
        if (task == g->task[i]) return true;
    return false;
}


// temporary buffers used when calculating a transition
static struct localTOp *localBuf = 0;
static struct initTOp  *initBuf = 0;
static struct sendTOp  *sendBuf = 0;
static struct recvTOp  *recvBuf = 0;
static struct redTOp   *redBuf = 0;
static int localBufSize = 0, localBufCount = 0;
static int initBufSize = 0, initBufCount = 0;
static int sendBufSize = 0, sendBufCount = 0;
static int recvBufSize = 0, recvBufCount = 0;
static int redBufSize = 0, redBufCount = 0;

static
void cleanTOpBufs(bool doFree)
{
    localBufCount = 0;
    initBufCount = 0;
    sendBufCount = 0;
    recvBufCount = 0;
    redBufCount = 0;
    if (doFree) {
        free(localBuf); localBufSize = 0;
        free(initBuf); initBufSize = 0;
        free(sendBuf); sendBufSize = 0;
        free(recvBuf); recvBufSize = 0;
        free(redBuf); redBufSize = 0;
    }
}

static
struct localTOp* appendLocalTOp(Laik_Slice* slc,
                                int fromSliceNo, int toSliceNo,
                                int fromMapNo, int toMapNo)
{
    if (localBufCount == localBufSize) {
        // enlarge temp buffer
        localBufSize = (localBufSize + 20) * 2;
        localBuf = realloc(localBuf, localBufSize * sizeof(struct localTOp));
        if (!localBuf) {
            laik_panic("Out of memory allocating memory for Laik_Transition");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    struct localTOp* op = &(localBuf[localBufCount]);
    localBufCount++;

    op->slc = *slc;
    op->fromSliceNo = fromSliceNo;
    op->toSliceNo = toSliceNo;
    op->fromMapNo = fromMapNo;
    op->toMapNo = toMapNo;

    return op;
}

static
struct initTOp* appendInitTOp(Laik_Slice* slc,
                              int sliceNo, int mapNo,
                              Laik_ReductionOperation redOp)
{
    if (initBufCount == initBufSize) {
        // enlarge temp buffer
        initBufSize = (initBufSize + 20) * 2;
        initBuf = realloc(initBuf, initBufSize * sizeof(struct initTOp));
        if (!initBuf) {
            laik_panic("Out of memory allocating memory for Laik_Transition");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    struct initTOp* op = &(initBuf[initBufCount]);
    initBufCount++;

    op->slc = *slc;
    op->sliceNo = sliceNo;
    op->mapNo = mapNo;
    op->redOp = redOp;

    return op;
}

static
struct sendTOp* appendSendTOp(Laik_Slice* slc,
                              int sliceNo, int mapNo, int toTask)
{
    if (sendBufCount == sendBufSize) {
        // enlarge temp buffer
        sendBufSize = (sendBufSize + 20) * 2;
        sendBuf = realloc(sendBuf, sendBufSize * sizeof(struct sendTOp));
        if (!sendBuf) {
            laik_panic("Out of memory allocating memory for Laik_Transition");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    struct sendTOp* op = &(sendBuf[sendBufCount]);
    sendBufCount++;

    op->slc = *slc;
    op->sliceNo = sliceNo;
    op->mapNo = mapNo;
    op->toTask = toTask;

    return op;
}

static
struct recvTOp* appendRecvTOp(Laik_Slice* slc,
                              int sliceNo, int mapNo, int fromTask)
{
    if (recvBufCount == recvBufSize) {
        // enlarge temp buffer
        recvBufSize = (recvBufSize + 20) * 2;
        recvBuf = realloc(recvBuf, recvBufSize * sizeof(struct recvTOp));
        if (!recvBuf) {
            laik_panic("Out of memory allocating memory for Laik_Transition");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    struct recvTOp* op = &(recvBuf[recvBufCount]);
    recvBufCount++;

    op->slc = *slc;
    op->sliceNo = sliceNo;
    op->mapNo = mapNo;
    op->fromTask = fromTask;

    return op;
}

static
struct redTOp* appendRedTOp(Laik_Slice* slc,
                            Laik_ReductionOperation redOp,
                            int inputGroup, int outputGroup,
                            int myInputSliceNo, int myOutputSliceNo,
                            int myInputMapNo, int myOutputMapNo)
{
    if (redBufCount == redBufSize) {
        // enlarge temp buffer
        redBufSize = (redBufSize + 20) * 2;
        redBuf = realloc(redBuf, redBufSize * sizeof(struct redTOp));
        if (!redBuf) {
            laik_panic("Out of memory allocating memory for Laik_Transition");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    struct redTOp* op = &(redBuf[redBufCount]);
    redBufCount++;

    op->slc = *slc;
    op->redOp = redOp;
    op->inputGroup = inputGroup;
    op->outputGroup = outputGroup;
    op->myInputSliceNo = myInputSliceNo;
    op->myOutputSliceNo = myOutputSliceNo;
    op->myInputMapNo = myInputMapNo;
    op->myOutputMapNo = myOutputMapNo;


    return op;
}


// find all slices where this task takes part in a reduction, and add
// them to the reduction operation list.
// TODO: we only support one mapping in each task for reductions
//       better: support multiple mappings for same index in same task
static
void calcAddReductions(int tflags,
                       Laik_Group* group,
                       Laik_ReductionOperation redOp,
                       Laik_Partitioning* fromP, Laik_Partitioning* toP)
{
    if (laik_log_begin(1)) {
        laik_log_append("calc '");
        laik_log_Reduction(redOp);
        laik_log_flush("' reduction actions:");
    }

    // TODO: only for 1d
    assert(fromP->space->dims == 1);

    // add slice borders of all tasks
    cleanBorderList();
    int sliceNo, lastTask, lastMapNo;
    sliceNo = 0;
    lastTask = -1;
    lastMapNo = -1;
    for(int i = 0; i < fromP->count; i++) {
        Laik_TaskSlice_Gen* ts = &(fromP->tslice[i]);
        // reset sliceNo to 0 on every task/mapNo change
        if ((ts->task != lastTask) || (ts->mapNo != lastMapNo)) {
            sliceNo = 0;
            lastTask = ts->task;
            lastMapNo = ts->mapNo;
        }
        appendBorder(ts->s.from.i[0], ts->task, sliceNo, ts->mapNo, true, true);
        appendBorder(ts->s.to.i[0], ts->task, sliceNo, ts->mapNo, false, true);
        sliceNo++;
    }
    lastTask = -1;
    lastMapNo = -1;
    for(int i = 0; i < toP->count; i++) {
        Laik_TaskSlice_Gen* ts = &(toP->tslice[i]);
        // reset sliceNo to 0 on every task/mapNo change
        if ((ts->task != lastTask) || (ts->mapNo != lastMapNo)) {
            sliceNo = 0;
            lastTask = ts->task;
            lastMapNo = ts->mapNo;
        }
        appendBorder(ts->s.from.i[0], ts->task, sliceNo, ts->mapNo, true, false);
        appendBorder(ts->s.to.i[0], ts->task, sliceNo, ts->mapNo, false, false);
        sliceNo++;
    }
    if (borderListCount == 0) return;

    // order by border to travers in border order
    qsort(borderList, borderListCount, sizeof(SliceBorder), sb_cmp);

#define MAX_TASKS 32
    int inputTask[MAX_TASKS], outputTask[MAX_TASKS];
    TaskGroup inputGroup, outputGroup;
    inputGroup.count = 0;
    inputGroup.task = inputTask;
    outputGroup.count = 0;
    outputGroup.task = outputTask;

    // travers borders and if this task has reduction input or wants output,
    // append reduction action to transaction

    int myid = group->myid;
    int myActivity = 0; // bit0: input, bit1: output
    int myInputSliceNo = -1, myOutputSliceNo = -1;
    int myInputMapNo = -1, myOutputMapNo = -1;
    Laik_Slice slc;
    for(int i = 0; i < borderListCount; i++) {
        SliceBorder* sb = &(borderList[i]);

#ifdef DEBUG_REDUCTIONSLICES
        laik_log(1, "at border %lld, task %d (slice %d, map %d): %s for %s",
                 (long long int) sb->b, sb->task, sb->sliceNo, sb->mapNo,
                 sb->isStart ? "start" : "end",
                 sb->isInput ? "input" : "output");
#endif

        // update input/output task lists and activity flags of this task
        bool isOk = true;
        if (sb->isInput) {
            if (sb->isStart) {
                isOk = addTask(&inputGroup, sb->task, MAX_TASKS);
                if (sb->task == myid) {
                    myActivity |= 1;
                    myInputSliceNo = sb->sliceNo;
                    myInputMapNo = sb->mapNo;
                }
            }
            else {
                isOk = removeTask(&inputGroup, sb->task);
                if (sb->task == myid) myActivity &= ~1;
            }
        }
        else {
            if (sb->isStart) {
                isOk = addTask(&outputGroup, sb->task, MAX_TASKS);
                if (sb->task == myid) {
                    myActivity |= 2;
                    myOutputSliceNo = sb->sliceNo;
                    myOutputMapNo = sb->mapNo;
                }
            }
            else {
                isOk = removeTask(&outputGroup, sb->task);
                if (sb->task == myid) myActivity &= ~2;
            }
        }
        assert(isOk);

        if ((i < borderListCount - 1) && (borderList[i + 1].b > sb->b)) {
            // about to leave a range with given input/output tasks
            int64_t nextBorder = borderList[i + 1].b;

#ifdef DEBUG_REDUCTIONSLICES
            char* act[] = {"(none)", "input", "output", "in & out"};
            laik_log(1, "  range (%lld - %lld), my activity: %s",
                     (long long int) sb->b,
                     (long long int) nextBorder, act[myActivity]);
#endif

            if (myActivity > 0) {
                assert(isInTaskGroup(&inputGroup, myid) ||
                       isInTaskGroup(&outputGroup, myid));

                slc.from.i[0] = sb->b;
                slc.to.i[0] = nextBorder;

                // check for special case: one input, ie. no reduction needed
                if (inputGroup.count == 1) {
                    if (inputGroup.task[0] == myid) {
                        // only this task as input

                        if ((outputGroup.count == 1) &&
                            (outputGroup.task[0] == myid)) {

                            // local (copy) operation
                            assert((redOp == LAIK_RO_Sum) || (redOp == LAIK_RO_None));
                            appendLocalTOp(&slc,
                                           myInputSliceNo, myOutputSliceNo,
                                           myInputMapNo, myOutputMapNo);
#ifdef DEBUG_REDUCTIONSLICES
                            laik_log(1, "  adding local (special reduction)"
                                        " (%lld - %lld) from %d/%d to %d/%d (slc/map)",
                                     (long long int) slc.from.i[0],
                                     (long long int) slc.to.i[0],
                                     myInputSliceNo, myInputMapNo,
                                     myOutputSliceNo, myOutputMapNo);
#endif
                            continue;
                        }

                        if (!(tflags & LAIK_TF_KEEP_REDUCTIONS)) {
                            // TODO: broadcasts might be supported in backend
                            for(int out = 0; out < outputGroup.count; out++) {
                                if (outputGroup.task[out] == myid) {
                                    // local (copy) operation
                                    assert((redOp == LAIK_RO_Sum) || (redOp == LAIK_RO_None));
                                    appendLocalTOp(&slc,
                                                   myInputSliceNo, myOutputSliceNo,
                                                   myInputMapNo, myOutputMapNo);
#ifdef DEBUG_REDUCTIONSLICES
                                    laik_log(1, "  adding local (special reduction)"
                                                " (%lld - %lld) from %d/%d to %d/%d (slc/map)",
                                             (long long int) slc.from.i[0],
                                            (long long int) slc.to.i[0],
                                            myInputSliceNo, myInputMapNo,
                                            myOutputSliceNo, myOutputMapNo);
#endif
                                    continue;
                                }

                                // send operation
                                assert((redOp == LAIK_RO_Sum) || (redOp == LAIK_RO_None));
                                appendSendTOp(&slc,
                                              myInputSliceNo, myInputMapNo,
                                              outputGroup.task[out]);
#ifdef DEBUG_REDUCTIONSLICES
                                laik_log(1, "  adding send (special reduction)"
                                            " (%lld - %lld) slc/map %d/%d to T%d",
                                         (long long int) slc.from.i[0],
                                        (long long int) slc.to.i[0],
                                        myInputSliceNo, myInputMapNo,
                                        outputGroup.task[out]);
#endif
                            }
                            continue;
                        }
                    } // only one input from this task
                    else {
                        if (!(tflags & LAIK_TF_KEEP_REDUCTIONS)) {
                            // one input from somebody else
                            for(int out = 0; out < outputGroup.count; out++) {
                                if (outputGroup.task[out] != myid) continue;

                                // receive operation
                                assert((redOp == LAIK_RO_Sum) || (redOp == LAIK_RO_None));
                                appendRecvTOp(&slc,
                                              myOutputSliceNo, myOutputMapNo,
                                              inputGroup.task[0]);

#ifdef DEBUG_REDUCTIONSLICES
                                laik_log(1, "  adding recv (special reduction)"
                                            " (%lld - %lld) slc/map %d/%d from T%d",
                                         (long long int) slc.from.i[0],
                                        (long long int) slc.to.i[0],
                                        myOutputSliceNo, myOutputMapNo,
                                        inputGroup.task[0]);
#endif
                            }
                            // handled cases with 1 input
                            continue;
                        }
                    }

                } // one input

                // add reduction operation
                int in = getTaskGroup(&inputGroup);
                int out = getTaskGroup(&outputGroup);

#ifdef DEBUG_REDUCTIONSLICES
                laik_log_begin(1);
                laik_log_append("  adding reduction (%lu - %lu), in %d:(",
                                slc.from.i[0], slc.to.i[0], in);
                for(int i = 0; i < groupList[in].count; i++) {
                    if (i > 0) laik_log_append(",");
                    laik_log_append("T%d", groupList[in].task[i]);
                }
                laik_log_append("), out %d:(", out);
                for(int i = 0; i < groupList[out].count; i++) {
                    if (i > 0) laik_log_append(",");
                    laik_log_append("T%d", groupList[out].task[i]);
                }
                laik_log_flush("), in %d/%d out %d/%d (slc/map)",
                               myInputSliceNo, myInputMapNo,
                               myOutputSliceNo, myOutputMapNo);
#endif

                // convert to all-group if possible
                if (groupList[in].count == group->size) in = -1;
                if (groupList[out].count == group->size) out = -1;

                // TODO: also specify reduction if it's just copy/send/recv
                redOp = LAIK_RO_Sum;

                assert(redOp != LAIK_RO_None); // must be a real reduction
                appendRedTOp(&slc, redOp, in, out,
                             myInputSliceNo, myOutputSliceNo,
                             myInputMapNo, myOutputMapNo);
            }
        }
    }
    // all tasks should be removed from input/output groups
    assert(inputGroup.count == 0);
    assert(outputGroup.count == 0);
}


// Calculate communication required for transitioning between partitionings
Laik_Transition*
laik_calc_transition(Laik_Space* space,
                     Laik_Partitioning* fromP, Laik_DataFlow fromFlow,
                     Laik_Partitioning* toP, Laik_DataFlow toFlow)
{
    Laik_Slice* slc;

    // flags for transition
    int tflags = 0; //LAIK_TF_KEEP_REDUCTIONS; // no_sendrev_actions

    cleanTOpBufs(false);
    cleanGroupList();

    // make sure requested operation is consistent
    Laik_Group* group = 0;
    if (fromP == 0) {
        // start: we come from nothing, go to initial partitioning
        assert(toP != 0);
        // FIXME: commented out to make exec_transition later happy
        //assert(!laik_do_copyin(toFlow));
        assert(toP->space == space);

        group = toP->group;
    }
    else if (toP == 0) {
        // end: go to nothing
        assert(fromP != 0);
        assert(!laik_do_copyout(fromFlow));
        assert(fromP->space == space);

        group = fromP->group;
    }
    else {
        // to and from set
        if (laik_do_copyin(toFlow)) {
            // values must come from something
            assert(laik_do_copyout(fromFlow) ||
                   laik_is_reduction(fromFlow));
        }
        assert(fromP->space == space);
        assert(toP->space == space);

        group = fromP->group;
        assert(toP->group == group);
    }
    assert(group != 0);

    // no action if not part of the group
    int myid = group->myid;
    if (myid == -1) return 0;

    int dims = space->dims;
    int taskCount = group->size;

    // init values as next phase does a reduction?
    if ((toP != 0) && laik_do_init(toFlow)) {

        for(int o = toP->off[myid]; o < toP->off[myid+1]; o++) {
            if (laik_slice_isEmpty(dims, &(toP->tslice[o].s))) continue;

            int redOp = laik_get_reduction(toFlow);
            assert(redOp != LAIK_RO_None);
            appendInitTOp( &(toP->tslice[o].s),
                           o - toP->off[myid],
                           toP->tslice[o].mapNo,
                           redOp);
        }
    }

    // check for 1d with preserving data between partitionings
    if ((dims == 1) &&
        (fromP != 0) && laik_do_copyout(fromFlow) &&
        (toP != 0) && laik_do_copyin(toFlow)) {

        // just check for reduction action
        // TODO: Do this always, remove other cases
        calcAddReductions(tflags,
                          group, laik_get_reduction(fromFlow),
                          fromP, toP);
    }
    else
    if ((fromP != 0) && (toP != 0)) {

        // determine local slices to keep
        // (may need local copy if from/to mappings are different).
        // reductions are not handled here, but by backend
        if (laik_do_copyout(fromFlow) && laik_do_copyin(toFlow)) {
            for(int o1 = fromP->off[myid]; o1 < fromP->off[myid+1]; o1++) {
                for(int o2 = toP->off[myid]; o2 < toP->off[myid+1]; o2++) {
                    slc = laik_slice_intersect(dims,
                                               &(fromP->tslice[o1].s),
                                               &(toP->tslice[o2].s));
                    if (slc == 0) continue;

                    appendLocalTOp(slc,
                                   o1 - fromP->off[myid],
                                   o2 - toP->off[myid],
                                   fromP->tslice[o1].mapNo,
                                   toP->tslice[o2].mapNo);
                }
            }
        }

        // something to reduce?
        if (laik_is_reduction(fromFlow) && laik_do_copyin(toFlow)) {
            // special case: reduction on full space involving everyone with
            //               result to one or all?
            bool fromAllto1OrAll = false;
            int outputGroup = -2;
            if (laik_partitioning_isAll(fromP)) {
                // reduction result either goes to all or master
                int task = laik_partitioning_isSingle(toP);
                if (task < 0) {
                    // output is not a single task
                    if (laik_partitioning_isAll(toP)) {
                        // output -1 is group ALL
                        outputGroup = -1;
                        fromAllto1OrAll = true;
                    }
                }
                else {
                    outputGroup = getTaskGroupSingle(task);
                    if (taskCount == 1) {
                        // the process group only consists of 1 process:
                        // one output process is equivalent to all
                        assert(task == 0); // must have rank/id 0
                        outputGroup = -1;
                    }
                    fromAllto1OrAll = true;
                }
            }

            if (fromAllto1OrAll) {
                assert(outputGroup > -2);
                // complete space, always sliceNo 0 and mapNo 0
                appendRedTOp( &(space->s),
                              laik_get_reduction(fromFlow),
                              -1, outputGroup, 0, 0, 0, 0);
            }
            else {
                assert(dims == 1);
                calcAddReductions(tflags,
                                  group, laik_get_reduction(fromFlow),
                                  fromP, toP);
            }
        }

        // something to send?
        if (laik_do_copyout(fromFlow)) {
            for(int task = 0; task < taskCount; task++) {
                if (task == myid) continue;
                for(int o1 = fromP->off[myid]; o1 < fromP->off[myid+1]; o1++) {

                    // everything the receiver has local, no need to send
                    // TODO: we only check for exact match to catch All
                    // FIXME: should print out a Warning/Error as the App
                    //        requests overwriting of values!
                    slc = &(fromP->tslice[o1].s);
                    for(int o2 = fromP->off[task]; o2 < fromP->off[task+1]; o2++) {
                        if (laik_slice_isEqual(dims, slc,
                                               &(fromP->tslice[o2].s))) {
                            slc = 0;
                            break;
                        }
                    }
                    if (slc == 0) continue;

                    // we may send multiple messages to same task
                    for(int o2 = toP->off[task]; o2 < toP->off[task+1]; o2++) {

                        slc = laik_slice_intersect(dims,
                                                   &(fromP->tslice[o1].s),
                                                   &(toP->tslice[o2].s));
                        if (slc == 0) continue;

                        appendSendTOp(slc, o1 - fromP->off[myid],
                                      fromP->tslice[o1].mapNo, task);
                    }
                }
            }
        }

        // something to receive not coming from a reduction?
        if (!laik_is_reduction(fromFlow) && laik_do_copyin(toFlow)) {
            for(int task = 0; task < taskCount; task++) {
                if (task == myid) continue;
                for(int o1 = toP->off[myid]; o1 < toP->off[myid+1]; o1++) {

                    // everything we have local will not have been sent
                    // TODO: we only check for exact match to catch All
                    // FIXME: should print out a Warning/Error as the App
                    //        was requesting for overwriting of values!
                    slc = &(toP->tslice[o1].s);
                    for(int o2 = fromP->off[myid]; o2 < fromP->off[myid+1]; o2++) {
                        if (laik_slice_isEqual(dims, slc,
                                               &(fromP->tslice[o2].s))) {
                            slc = 0;
                            break;
                        }
                    }
                    if (slc == 0) continue;

                    for(int o2 = fromP->off[task]; o2 < fromP->off[task+1]; o2++) {

                        slc = laik_slice_intersect(dims,
                                                   &(fromP->tslice[o2].s),
                                                   &(toP->tslice[o1].s));
                        if (slc == 0) continue;

                        appendRecvTOp(slc, o1 - toP->off[myid],
                                      toP->tslice[o1].mapNo, task);
                    }
                }
            }
        }
    }

    // allocate space as needed
    int localSize = localBufCount * sizeof(struct localTOp);
    int initSize  = initBufCount  * sizeof(struct initTOp);
    int sendSize  = sendBufCount  * sizeof(struct sendTOp);
    int recvSize  = recvBufCount  * sizeof(struct recvTOp);
    int redSize   = redBufCount   * sizeof(struct redTOp);
    // we copy group list into transition object
    int gListSize = groupListCount * sizeof(TaskGroup);
    int tListSize = 0;
    for (int i = 0; i < groupListCount; i++)
        tListSize += groupList[i].count * sizeof(int);

    int tsize = sizeof(Laik_Transition) + gListSize + tListSize +
                localSize + initSize + sendSize + recvSize + redSize;
    int localOff = sizeof(Laik_Transition);
    int initOff  = localOff + localSize;
    int sendOff  = initOff  + initSize;
    int recvOff  = sendOff  + sendSize;
    int redOff   = recvOff  + recvSize;
    int gListOff = redOff   + redSize;
    int tListOff = gListOff + gListSize;
    assert(tListOff + tListSize == tsize);

    Laik_Transition* t = malloc(tsize);
    if (!t) {
        laik_log(LAIK_LL_Panic,
                 "Out of memory allocating Laik_Transition object, size %d",
                 tsize);
        exit(1); // not actually needed, laik_panic never returns
    }

    t->flags = tflags;
    t->space = space;
    t->group = group;
    t->fromPartitioning = fromP;
    t->toPartitioning = toP;
    t->fromFlow = fromFlow;
    t->toFlow = toFlow;

    t->dims = dims;
    t->actionCount = localBufCount + initBufCount +
                     sendBufCount + recvBufCount + redBufCount;
    t->local = (struct localTOp*) (((char*)t) + localOff);
    t->init  = (struct initTOp*)  (((char*)t) + initOff);
    t->send  = (struct sendTOp*)  (((char*)t) + sendOff);
    t->recv  = (struct recvTOp*)  (((char*)t) + recvOff);
    t->red   = (struct redTOp*)   (((char*)t) + redOff);
    t->subgroup = (TaskGroup*)       (((char*)t) + gListOff);
    t->localCount = localBufCount;
    t->initCount  = initBufCount;
    t->sendCount  = sendBufCount;
    t->recvCount  = recvBufCount;
    t->redCount   = redBufCount;
    t->subgroupCount = groupListCount;
    memcpy(t->local, localBuf, localSize);
    memcpy(t->init, initBuf,  initSize);
    memcpy(t->send, sendBuf,  sendSize);
    memcpy(t->recv, recvBuf,  recvSize);
    memcpy(t->red,  redBuf,   redSize);

    // copy group list and task list of each group into transition object
    char* tList = ((char*)t) + tListOff;
    for (int i = 0; i < groupListCount; i++) {
        t->subgroup[i].count = groupList[i].count;
        t->subgroup[i].task = (int*) tList;
        tListSize = groupList[i].count * sizeof(int);
        memcpy(tList, groupList[i].task, tListSize);
        tList += tListSize;
    }
    assert(tList == ((char*)t) + tsize);

    if (laik_log_begin(1)) {
        laik_log_Transition(t, true);
        laik_log_flush(0);
    }

    return t;
}


// Calculate communication for transitioning between partitioning groups
Laik_Transition* laik_calc_transitionG(Laik_PartGroup* from,
                                       Laik_PartGroup* to)
{
    (void) from; /* FIXME: Why have this parameter if it's never used */
    (void) to;   /* FIXME: Why have this parameter if it's never used */

    // Laik_Transition* t;

    assert(0); // TODO
}

// enforce consistency for the partitioning group, depending on previous
void laik_enforce_consistency(Laik_Instance* i, Laik_PartGroup* g)
{
    (void) i; /* FIXME: Why have this parameter if it's never used */
    (void) g; /* FIXME: Why have this parameter if it's never used */

    assert(0); // TODO
}


// couple different LAIK instances via spaces:
// one partition of calling task in outer space is mapped to inner space
void laik_couple_nested(Laik_Space* outer, Laik_Space* inner)
{
    (void) outer; /* FIXME: Why have this parameter if it's never used */
    (void) inner; /* FIXME: Why have this parameter if it's never used */

    assert(0); // TODO
}

// migrate a partitioning defined on one task group to another group
// (no repartitioning: only works if partitions of removed tasks are empty)
bool laik_migrate_phase(Laik_AccessPhase* ap, Laik_Group* newg)
{
    Laik_Group* oldg = ap->group;
    assert(oldg != newg);

    if (ap->hasValidPartitioning) {
        assert(ap->partitioning && (ap->partitioning->group == oldg));
        laik_partitioning_migrate(ap->partitioning, newg);
    }

    laik_removeAccessPhaseForGroup(oldg, ap);
    laik_addAcessPhaseForGroup(newg, ap);
    ap->group = newg;

    // make partitioning users (data containers) migrate to new group
    Laik_Data* d = ap->firstDataForAccessPhase;
    while(d) {
        if (d->activePartitioning) {
            if (d->activePartitioning->group == oldg)
                laik_partitioning_migrate(d->activePartitioning, newg);
        }
        d = d->nextAccessPhaseUser;
    }


    return true;
}

// migrate a partitioning defined on one task group to another group
// for the required repartitioning, either use the default partitioner
// or the given one. In the latter case, the partitioner is run
// on old group and is expected to produce no partitions for tasks
// to be removed
void laik_migrate_and_repartition(Laik_AccessPhase* ap, Laik_Group* newg,
                                  Laik_Partitioner* pr)
{
    if (!ap) return;

    Laik_Partitioning* p;
    if (pr) {
        p = laik_new_partitioning(pr, ap->group, ap->space,
                                  ap->hasValidPartitioning ? ap->partitioning : 0);
    }
    else {
        p = laik_new_partitioning(ap->partitioner, newg, ap->space, 0);
        laik_partitioning_migrate(p, ap->group);
    }
    laik_phase_set_partitioning(ap, p);
    bool res = laik_migrate_phase(ap, newg);
    assert(res);
}





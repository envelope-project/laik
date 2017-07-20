/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// counter for space ID, just for debugging
static int space_id = 0;

// counter for partitioning ID, just for debugging
static int part_id = 0;

// helpers

void setIndex(Laik_Index* i, uint64_t i1, uint64_t i2, uint64_t i3)
{
    i->i[0] = i1;
    i->i[1] = i2;
    i->i[2] = i3;
}

static
int getSpaceStr(char* s, Laik_Space* spc)
{
    switch(spc->dims) {
    case 1:
        return sprintf(s, "[0-%llu]",
                       (unsigned long long) spc->size[0]-1);
    case 2:
        return sprintf(s, "[0-%llu/0-%llu]",
                       (unsigned long long) spc->size[0]-1,
                       (unsigned long long) spc->size[1]-1);
    case 3:
        return sprintf(s, "[0-%llu/0-%llu/0-%llu]",
                       (unsigned long long) spc->size[0]-1,
                       (unsigned long long) spc->size[1]-1,
                       (unsigned long long) spc->size[2]-1);
    default: assert(0);
    }
    return 0;
}


int laik_getIndexStr(char* s, int dims, Laik_Index* idx, bool minus1)
{
    uint64_t i1 = idx->i[0];
    uint64_t i2 = idx->i[1];
    uint64_t i3 = idx->i[2];
    if (minus1) {
        i1--;
        i2--;
        i3--;
    }

    switch(dims) {
    case 1:
        return sprintf(s, "%llu", (unsigned long long) i1);
    case 2:
        return sprintf(s, "%llu/%llu",
                       (unsigned long long) i1,
                       (unsigned long long) i2);
    case 3:
        return sprintf(s, "%llu/%llu/%llu",
                       (unsigned long long) i1,
                       (unsigned long long) i2,
                       (unsigned long long) i3);
    default: assert(0);
    }
    return 0;
}

bool laik_index_isEqual(int dims, Laik_Index* i1, Laik_Index* i2)
{
    if (i1->i[0] != i2->i[0]) return false;
    if (dims == 1) return true;

    if (i1->i[1] != i2->i[1]) return false;
    if (dims == 1) return true;

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
bool intersectRange(uint64_t from1, uint64_t to1, uint64_t from2, uint64_t to2,
                    uint64_t* resFrom, uint64_t* resTo)
{
    if (from1 >= to2) return false;
    if (from2 >= to1) return false;
    *resFrom = (from1 > from2) ? from1 : from2;
    *resTo = (to1 > to2) ? to2 : to1;
    return true;
}

// get the intersection of 2 slices; return 0 if intersection is empty
Laik_Slice* laik_slice_intersect(int dims, Laik_Slice* s1, Laik_Slice* s2)
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

bool laik_slice_isEqual(int dims, Laik_Slice* s1, Laik_Slice* s2)
{
    if (!laik_index_isEqual(dims, &(s1->from), &(s2->from))) return false;
    if (!laik_index_isEqual(dims, &(s1->to), &(s2->to))) return false;
    return true;
}


static
Laik_Slice* sliceFromSpace(Laik_Space* s)
{
    static Laik_Slice slc;

    slc.from.i[0] = 0;
    slc.from.i[1] = 0;
    slc.from.i[2] = 0;
    slc.to.i[0] = s->size[0];
    slc.to.i[1] = s->size[1];
    slc.to.i[2] = s->size[2];

    return &slc;
}

static
int getSliceStr(char* s, int dims, Laik_Slice* slc)
{
    if (laik_slice_isEmpty(dims, slc))
        return sprintf(s, "(empty)");

    int off;
    off  = sprintf(s, "[");
    off += laik_getIndexStr(s+off, dims, &(slc->from), false);
    off += sprintf(s+off, "-");
    off += laik_getIndexStr(s+off, dims, &(slc->to), true);
    off += sprintf(s+off, "]");
    return off;
}


static
int getPartitioningTypeStr(char* s, Laik_PartitionType type)
{
    switch(type) {
    case LAIK_PT_All:    return sprintf(s, "all");
    case LAIK_PT_Block:  return sprintf(s, "block");
    case LAIK_PT_Master: return sprintf(s, "master");
    case LAIK_PT_Copy:   return sprintf(s, "copy");
    default: assert(0);
    }
    return 0;
}

static
int getReductionStr(char* s, Laik_ReductionOperation op)
{
    switch(op) {
    case LAIK_RO_None: return sprintf(s, "none");
    case LAIK_RO_Sum:  return sprintf(s, "sum");
    default: assert(0);
    }
    return 0;
}


static
int getDataFlowStr(char* s, Laik_DataFlow flow)
{
    int o = 0;

    if (flow & LAIK_DF_CopyIn)    o += sprintf(s+o, "copyin|");
    if (flow & LAIK_DF_CopyOut)   o += sprintf(s+o, "copyout|");
    if (flow & LAIK_DF_Init)      o += sprintf(s+o, "init|");
    if (flow & LAIK_DF_ReduceOut) o += sprintf(s+o, "reduceout|");
    if (flow & LAIK_DF_Sum)       o += sprintf(s+o, "sum|");
    if (o > 0) {
        o--;
        s[o] = 0;
    }
    else
        o += sprintf(s+o, "none");

    return o;
}


static
int getTransitionStr(char* s, Laik_Transition* t)
{
    int off = 0;

    if (t->localCount>0) {
        off += sprintf(s+off, "  local: ");
        for(int i=0; i<t->localCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += getSliceStr(s+off, t->dims, &(t->local[i].slc));
        }
        off += sprintf(s+off, "\n");
    }

    if (t->initCount>0) {
        off += sprintf(s+off, "  init: ");
        for(int i=0; i<t->initCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += getReductionStr(s+off, t->init[i].redOp);
            off += getSliceStr(s+off, t->dims, &(t->init[i].slc));
        }
        off += sprintf(s+off, "\n");
    }

    if (t->sendCount>0) {
        off += sprintf(s+off, "  send: ");
        for(int i=0; i<t->sendCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += getSliceStr(s+off, t->dims, &(t->send[i].slc));
            off += sprintf(s+off, " => T%d", t->send[i].toTask);
        }
        off += sprintf(s+off, "\n");
    }

    if (t->recvCount>0) {
        off += sprintf(s+off, "  recv: ");
        for(int i=0; i<t->recvCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += sprintf(s+off, "T%d => ", t->recv[i].fromTask);
            off += getSliceStr(s+off, t->dims, &(t->recv[i].slc));
        }
        off += sprintf(s+off, "\n");
    }

    if (t->redCount>0) {
        off += sprintf(s+off, "  reduction: ");
        for(int i=0; i<t->redCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += getReductionStr(s+off, t->red[i].redOp);
            off += getSliceStr(s+off, t->dims, &(t->red[i].slc));
            off += sprintf(s+off, " => %s (%d)",
                           (t->red[i].rootTask == -1) ? "all":"master",
                           t->red[i].rootTask);
        }
        off += sprintf(s+off, "\n");
    }

    if (off == 0) s[0] = 0;
    return off;
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
Laik_Space* laik_new_space(Laik_Instance* i)
{
    Laik_Space* space = (Laik_Space*) malloc(sizeof(Laik_Space));

    space->id = space_id++;
    space->name = strdup("space-0     ");
    sprintf(space->name, "space-%d", space->id);

    space->inst = i;
    space->dims = 0; // invalid
    space->firstSpaceUser = 0;

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

    if (laik_logshown(1)) {
        char s[100];
        getSpaceStr(s, space);
        laik_log(1, "new 1d space '%s': %s\n", space->name, s);
    }

    return space;
}

Laik_Space* laik_new_space_2d(Laik_Instance* i,
                              uint64_t s1, uint64_t s2)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 2;
    space->size[0] = s1;
    space->size[1] = s2;

    if (laik_logshown(1)) {
        char s[100];
        getSpaceStr(s, space);
        laik_log(1, "new 2d space '%s': %s\n", space->name, s);
    }

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

    if (laik_logshown(1)) {
        char s[100];
        getSpaceStr(s, space);
        laik_log(1, "new 3d space '%s': %s\n", space->name, s);
    }

    return space;
}

// free a space with all resources depending on it (e.g. paritionings)
void laik_free_space(Laik_Space* s)
{
    free(s->name);

    // TODO
}

// set a space a name, for debug output
void laik_set_space_name(Laik_Space* s, char* n)
{
    s->name = strdup(n);
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


void laik_addSpaceUser(Laik_Space* s, Laik_Partitioning* p)
{
    assert(p->nextSpaceUser == 0);
    p->nextSpaceUser = s->firstSpaceUser;
    s->firstSpaceUser = p;
}

void laik_removeSpaceUser(Laik_Space* s, Laik_Partitioning* p)
{
    if (s->firstSpaceUser == p) {
        s->firstSpaceUser = p->nextSpaceUser;
    }
    else {
        // search for previous item
        Laik_Partitioning* pp = s->firstSpaceUser;
        while(pp->nextSpaceUser != p)
            pp = pp->nextSpaceUser;
        assert(pp != 0); // not found, should not happen
        pp->nextSpaceUser = p->nextSpaceUser;
    }
    p->nextSpaceUser = 0;
}



//-----------------------
// Laik_BorderArray

Laik_BorderArray* allocBorders(int tasks, int capacity)
{
    Laik_BorderArray* a;

    // allocate struct with offset and slice arrays afterwards
    a = (Laik_BorderArray*) malloc(sizeof(Laik_BorderArray) +
                                   (tasks + 1) * sizeof(int) +
                                   capacity * sizeof(Laik_TaskSlice));
    a->off = (int*) ((char*) a + sizeof(Laik_BorderArray));
    a->tslice = (Laik_TaskSlice*) ((char*) a->off + (tasks + 1) * sizeof(int));
    a->tasks = tasks;
    a->capacity = capacity;
    a->count = 0;

    return a;
}

void appendSlice(Laik_BorderArray* a, int task, Laik_Slice* s)
{
    assert(a->count < a->capacity);
    a->tslice[a->count].task = task;
    a->tslice[a->count].s = *s;
    a->count++;
}

static
int ts_cmp(const void *p1, const void *p2)
{
    const Laik_TaskSlice* ts1 = (const Laik_TaskSlice*) p1;
    const Laik_TaskSlice* ts2 = (const Laik_TaskSlice*) p2;
    return ts1->task - ts2->task;
}

void updateBorderArrayOffsets(Laik_BorderArray* a)
{
    int task, off = 0;
    for(task = 0; task < a->tasks; task++) {
        a->off[task] = off;
        while((off < a->count) && (a->tslice[off].task <= task))
            off++;
    }
    a->off[task] = off;
    assert(off == a->count);
}

void sortBorderArray(Laik_BorderArray* a)
{
    qsort( &(a->tslice[0]), a->count, sizeof(Laik_TaskSlice), ts_cmp);
    updateBorderArrayOffsets(a);
}

void clearBorderArray(Laik_BorderArray* a)
{
    // to remove all entries, it's enough to set count to 0
    a->count = 0;
}


//-----------------------
// Laik_Partitioning


// create a new partitioning on a space
Laik_Partitioning* laik_new_partitioning(Laik_Group* g, Laik_Space* s)
{
    Laik_Partitioning* p;
    p = (Laik_Partitioning*) malloc(sizeof(Laik_Partitioning));

    p->id = part_id++;
    p->name = strdup("partng-0     ");
    sprintf(p->name, "partng-%d", p->id);

    assert(g->inst == s->inst);
    p->group = g;
    p->space = s;
    p->pdim = 0;

    p->flow = LAIK_DF_None;
    p->type = LAIK_PT_None;
    p->copyIn = false;
    p->copyOut = false;
    p->redOp = LAIK_RO_None;

    p->partitioner = 0;
    p->base = 0;

    p->bordersValid = false;
    p->borders = 0;

    p->nextSpaceUser = 0;
    p->nextGroupUser = 0;
    p->firstPartitioningUser = 0;

    laik_addSpaceUser(s, p);
    laik_addGroupUser(p->group, p);
    return p;
}

static
void set_flow(Laik_Partitioning* p, Laik_DataFlow flow)
{
    p->flow = flow;
    p->copyIn = laik_do_copyin(flow);
    p->copyOut = laik_do_copyout(flow);
    p->redOp = laik_get_reduction(flow);
}

Laik_Partitioning*
laik_new_base_partitioning(Laik_Group* g, Laik_Space* space,
                           Laik_PartitionType pt,
                           Laik_DataFlow flow)
{
    Laik_Partitioning* p;
    p = laik_new_partitioning(g, space);
    p->type = pt;
    set_flow(p, flow);

    if (laik_logshown(1)) {
        char s[100];
        getPartitioningTypeStr(s, p->type);
        getDataFlowStr(s+50, p->flow);
        laik_log(1, "new partitioning '%s': type %s, data flow %s, group %d\n",
                 p->name, s, s+50, p->group->gid);
    }

    return p;
}


void laik_addPartitioningUser(Laik_Partitioning* p, Laik_Data* d)
{
    assert(d->nextPartitioningUser == 0);
    d->nextPartitioningUser = p->firstPartitioningUser;
    p->firstPartitioningUser = d;
}

void laik_removePartitioningUser(Laik_Partitioning* p, Laik_Data* d)
{
    if (p->firstPartitioningUser == d) {
        p->firstPartitioningUser = d->nextPartitioningUser;
    }
    else {
        // search for previous item
        Laik_Data* dd = p->firstPartitioningUser;
        while(dd->nextPartitioningUser != d)
            dd = dd->nextPartitioningUser;
        assert(dd != 0); // not found, should not happen
        dd->nextPartitioningUser = d->nextPartitioningUser;
    }
    d->nextPartitioningUser = 0;
}


Laik_Partitioner* laik_get_partitioner(Laik_Partitioning* p)
{
    if (!p->partitioner) {
        switch(p->type) {
        case LAIK_PT_Block:
            p->partitioner = laik_newBlockPartitioner(p);
            break;
        case LAIK_PT_All:
        case LAIK_PT_Master:
        case LAIK_PT_Copy:
            // built-in algorithms do not require partitioner
            break;
        default:
            laik_log(LAIK_LL_Panic, "Not Implemented!\n");
            break;
        }
    }
    return p->partitioner;
}

void laik_set_partitioner(Laik_Partitioning* p, Laik_Partitioner* pr)
{
    assert(pr->type != LAIK_PT_None);
    p->type = pr->type;
    p->partitioner = pr;
}



// for multiple-dimensional spaces, set dimension to partition (default is 0)
void laik_set_partitioning_dimension(Laik_Partitioning* p, int d)
{
    assert((d >= 0) && (d < p->space->dims));
    p->pdim = d;
}


// create a new partitioning based on another one on the same space
Laik_Partitioning*
laik_new_coupled_partitioning(Laik_Partitioning* base,
                              Laik_PartitionType pt,
                              Laik_DataFlow flow)
{
    Laik_Partitioning* p;
    p = laik_new_partitioning(base->group, base->space);
    p->type = pt;
    p->base = base;
    set_flow(p, flow);

    return p;
}

// create a new partitioning based on another one on a different space
// this also needs to know which dimensions should be coupled
Laik_Partitioning*
laik_new_spacecoupled_partitioning(Laik_Partitioning* base,
                                   Laik_Space* s, int from, int to,
                                   Laik_PartitionType pt,
                                   Laik_DataFlow flow)
{
    Laik_Partitioning* p;
    p = laik_new_partitioning(base->group, s);
    p->type = pt;
    p->base = base;
    set_flow(p, flow);

    assert(0); // TODO

    return p;
}

// free a partitioning with related resources
void laik_free_partitioning(Laik_Partitioning* p)
{
    // FIXME: needs some kind of reference counting
    return;
    if (p->firstPartitioningUser == 0) {
        laik_removeGroupUser(p->group, p);
        laik_removeSpaceUser(p->space, p);
        free(p->name);
        free(p->borders);
    }
}

// get number of slices of this task
int laik_my_slicecount(Laik_Partitioning* p)
{
    laik_update_partitioning(p);

    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group
    assert(myid < p->group->size);
    return p->borders->off[myid+1] - p->borders->off[myid];
}

// get slice number <n> from the slices of this task
Laik_Slice* laik_my_slice(Laik_Partitioning* p, int n)
{
    laik_update_partitioning(p);

    int myid = p->group->myid;
    int o = p->borders->off[myid] + n;
    if (o >= p->borders->off[myid+1]) {
        // slice <n> is invalid
        return 0;
    }
    assert(p->borders->tslice[o].task == myid);
    return &(p->borders->tslice[o].s);
}


// give a partitioning a name, for debug output
void laik_set_partitioning_name(Laik_Partitioning* p, char* n)
{
    p->name = strdup(n);
}


// make sure partitioning borders are up to date
// returns true on changes (if borders had to be updated)
bool laik_update_partitioning(Laik_Partitioning* p)
{
    Laik_BorderArray* baseBorders = 0;
    Laik_Space* s = p->space;
    int pdim = p->pdim;
    int basepdim = 0;

    if (p->base) {
        if (laik_update_partitioning(p->base))
            p->bordersValid = false;

        baseBorders = p->base->borders;
        basepdim = p->base->pdim;
        // sizes of coupled dimensions should be equal
        assert(s->size[pdim] == p->base->space->size[basepdim]);
    }

    if (p->bordersValid)
        return false;

    int count = p->group->size;
    if (!p->borders)
        p->borders = allocBorders(count, 2 * count);
    else
        clearBorderArray(p->borders);
    Laik_BorderArray* ba = p->borders;

    // may trigger creation of partitioner object
    Laik_Partitioner* pr = laik_get_partitioner(p);
    if (pr)
        (pr->run)(pr, ba);
    else {
        // handle simple built-in partitioning algorithms

        Laik_Slice slc;

        switch(p->type) {
        case LAIK_PT_All:
            for(int task = 0; task < ba->tasks; task++) {
                setIndex(&(slc.from), 0, 0, 0);
                setIndex(&(slc.to), s->size[0], s->size[1], s->size[2]);
                appendSlice(ba, task, &slc);
            }
            break;

        case LAIK_PT_Master:
            // only full slice for master
            setIndex(&(slc.from), 0, 0, 0);
            setIndex(&(slc.to), s->size[0], s->size[1], s->size[2]);
            appendSlice(ba, 0, &slc);
            break;

        case LAIK_PT_Copy:
            assert(baseBorders);
            for(int i = 0; i < baseBorders->count; i++) {
                setIndex(&(slc.from), 0, 0, 0);
                setIndex(&(slc.to), s->size[0], s->size[1], s->size[2]);
                slc.from.i[pdim] = baseBorders->tslice[i].s.from.i[basepdim];
                slc.to.i[pdim] = baseBorders->tslice[i].s.to.i[basepdim];
                appendSlice(ba, baseBorders->tslice[i].task, &slc);
            }
            break;

        default:
            assert(0); // TODO
            break;
        }
    }

    sortBorderArray(ba);
    p->bordersValid = true;

    if (laik_logshown(1)) {
        char str[1000];
        int off;
        off = sprintf(str, "partitioning '%s' (group %d) updated: ",
                      p->name, p->group->gid);
        for(int i = 0; i < ba->count; i++) {
            if (i>0)
                off += sprintf(str+off, ", ");
            off += sprintf(str+off, "%d:", p->borders->tslice[i].task);
            off += getSliceStr(str+off, p->space->dims,
                               &(p->borders->tslice[i].s));
        }
        laik_log(1, "%s\n", str);
    }

    return true;
}



// append a partitioning to a partioning group whose consistency should
// be enforced at the same point in time
void laik_append_partitioning(Laik_PartGroup* g, Laik_Partitioning* p)
{
    assert(0); // TODO
}

// Calculate communication required for transitioning between partitionings
Laik_Transition* laik_calc_transitionP(Laik_Partitioning* from,
                                       Laik_Partitioning* to)
{
    Laik_Slice* slc;

#define TRANSSLICES_MAX 100
    struct localTOp local[TRANSSLICES_MAX];
    struct initTOp init[TRANSSLICES_MAX];
    struct sendTOp send[TRANSSLICES_MAX];
    struct recvTOp recv[TRANSSLICES_MAX];
    struct redTOp red[TRANSSLICES_MAX];
    int localCount, initCount, sendCount, recvCount, redCount;

    localCount = 0;
    initCount = 0;
    sendCount = 0;
    recvCount = 0;
    redCount = 0;

    // either one of <from> and <to> has to be valid; same space, same group
    Laik_Space* space = 0;
    Laik_Group* group = 0;

    // make sure requested data flow is consistent
    if (from == 0) {
        // start: we come from nothing, go to initial partitioning
        assert(to != 0);
        assert(!laik_do_copyin(to->flow));

        space = to->space;
        group = to->group;
    }
    else if (to == 0) {
        // end: go to nothing
        assert(from != 0);
        assert(!laik_do_copyout(from->flow));

        space = from->space;
        group = from->group;
    }
    else {
        // to and from set
        if (laik_do_copyin(to->flow)) {
            // values must come from something
            assert(laik_do_copyout(from->flow) ||
                   laik_is_reduction(from->flow));
        }
        assert(from->space == to->space);
        assert(from->group == to->group);
        space = from->space;
        group = from->group;
    }

    int dims = space->dims;
    int myid = group->inst->myid;
    int count = group->size;

    Laik_BorderArray* fromBA = from ? from->borders : 0;
    Laik_BorderArray* toBA = to ? to->borders : 0;

    // init values as next phase does a reduction?
    if ((to != 0) && laik_is_reduction(to->flow)) {

        for(int o = toBA->off[myid]; o < toBA->off[myid+1]; o++) {
            if (laik_slice_isEmpty(dims, &(toBA->tslice[o].s))) continue;
            assert(initCount < TRANSSLICES_MAX);
            assert(to->redOp != LAIK_RO_None);
            struct initTOp* op = &(init[initCount]);
            op->slc = toBA->tslice[o].s;
            op->sliceNo = o - toBA->off[myid];
            op->redOp = to->redOp;
            initCount++;
        }
    }

    if ((from != 0) && (to != 0)) {

        // determine local slices to keep
        // (may need local copy if from/to mappings are different).
        // reductions are not handled here, but by backend
        if (laik_do_copyout(from->flow) && laik_do_copyin(to->flow)) {
            for(int o1 = fromBA->off[myid]; o1 < fromBA->off[myid+1]; o1++) {
                for(int o2 = toBA->off[myid]; o2 < toBA->off[myid+1]; o2++) {
                    slc = laik_slice_intersect(dims,
                                               &(fromBA->tslice[o1].s),
                                               &(toBA->tslice[o2].s));
                    if (slc == 0) continue;

                    assert(localCount < TRANSSLICES_MAX);
                    struct localTOp* op = &(local[localCount]);
                    op->slc = *slc;
                    op->fromSliceNo = o1 - fromBA->off[myid];
                    op->toSliceNo = o2 - toBA->off[myid];

                    localCount++;
                }
            }
        }

        // something to reduce?
        if (laik_is_reduction(from->flow)) {
            // reductions always should involve everyone
            assert(from->type == LAIK_PT_All);
            if (laik_do_copyin(to->flow)) {
                assert(redCount < TRANSSLICES_MAX);
                assert((to->type == LAIK_PT_Master) ||
                       (to->type == LAIK_PT_All));

                struct redTOp* op = &(red[redCount]);
                op->slc = *sliceFromSpace(from->space); // complete space
                op->redOp = from->redOp;
                op->rootTask = (to->type == LAIK_PT_All) ? -1 : 0;

                redCount++;
            }
        }

        // something to send?
        if (laik_do_copyout(from->flow)) {
            for(int o1 = fromBA->off[myid]; o1 < fromBA->off[myid+1]; o1++) {
                for(int task = 0; task < count; task++) {
                    if (task == myid) continue;
                    // we may send multiple messages to same task
                    for(int o2 = toBA->off[task]; o2 < toBA->off[task+1]; o2++) {

                        slc = laik_slice_intersect(dims,
                                                   &(fromBA->tslice[o1].s),
                                                   &(toBA->tslice[o2].s));
                        if (slc == 0) continue;

                        assert(sendCount < TRANSSLICES_MAX);
                        struct sendTOp* op = &(send[sendCount]);
                        op->slc = *slc;
                        op->sliceNo = o1 - fromBA->off[myid];
                        op->toTask = task;
                        sendCount++;
                    }
                }
            }
        }

        // something to receive not coming from a reduction?
        if (!laik_is_reduction(from->flow) && laik_do_copyin(to->flow)) {
            for(int task = 0; task < count; task++) {
                if (task == myid) continue;
                for(int o1 = fromBA->off[task]; o1 < fromBA->off[task+1]; o1++) {
                    for(int o2 = toBA->off[myid]; o2 < toBA->off[myid+1]; o2++) {

                        slc = laik_slice_intersect(dims,
                                                   &(fromBA->tslice[o1].s),
                                                   &(toBA->tslice[o2].s));
                        if (slc == 0) continue;

                        assert(recvCount < TRANSSLICES_MAX);
                        struct recvTOp* op = &(recv[recvCount]);
                        op->slc = *slc;
                        op->sliceNo = o2 - toBA->off[myid];
                        op->fromTask = task;
                        recvCount++;
                    }
                }
            }
        }
    }

    // allocate space as needed
    int localSize = localCount * sizeof(struct localTOp);
    int initSize  = initCount  * sizeof(struct initTOp);
    int sendSize  = sendCount  * sizeof(struct sendTOp);
    int recvSize  = recvCount  * sizeof(struct recvTOp);
    int redSize   = redCount   * sizeof(struct redTOp);
    int tsize = sizeof(Laik_Transition) +
                localSize + initSize + sendSize + recvSize + redSize;
    int localOff = sizeof(Laik_Transition);
    int initOff  = localOff + localSize;
    int sendOff  = initOff  + initSize;
    int recvOff  = sendOff  + sendSize;
    int redOff   = recvOff  + recvSize;
    assert(redOff + redSize == tsize);

    Laik_Transition* t = (Laik_Transition*) malloc(tsize);
    t->dims = dims;
    t->local = (struct localTOp*) (((char*)t) + localOff);
    t->init  = (struct initTOp*)  (((char*)t) + initOff);
    t->send  = (struct sendTOp*)  (((char*)t) + sendOff);
    t->recv  = (struct recvTOp*)  (((char*)t) + recvOff);
    t->red   = (struct redTOp*)   (((char*)t) + redOff);
    t->localCount = localCount;
    t->initCount  = initCount;
    t->sendCount  = sendCount;
    t->recvCount  = recvCount;
    t->redCount   = redCount;
    memcpy(t->local, local, localSize);
    memcpy(t->init, init,  initSize);
    memcpy(t->send, send,  sendSize);
    memcpy(t->recv, recv,  recvSize);
    memcpy(t->red,  red,   redSize);

    if (laik_logshown(1)) {
        char s[1000];
        int len = getTransitionStr(s, t);
        if (len == 0)
            laik_log(1, "transition %s => %s: (nothing)\n",
                     from ? from->name : "(none)", to ? to->name : "(none)");
        else
            laik_log(1, "transition %s => %s:\n%s",
                     from ? from->name : "(none)", to ? to->name : "(none)",
                     s);
    }

    return t;
}

// Calculate communication for transitioning between partitioning groups
Laik_Transition* laik_calc_transitionG(Laik_PartGroup* from,
                                       Laik_PartGroup* to)
{
    // Laik_Transition* t;

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


// migrate a partitioning defined on one task group to another group
bool laik_partitioning_migrate(Laik_Partitioning* p, Laik_Group* g)
{
    Laik_Group* oldg = p->group;
    // only migration to shrinked group, with parent being old group
    assert(g->parent == oldg);
    if (p->base) {
        if (!laik_partitioning_migrate(p->base, g))
            return false;
    }
    if (p->bordersValid) {
        // need to migrate IDs in borders
        assert(p->borders && (p->borders->tasks == oldg->size));
        assert(g->size < oldg->size); // TODO: only shrinking

        // check that partitions of all task to be removed are empty
        for(int i = 0; i < oldg->size; i++) {
            if (g->fromParent[i] < 0)
                if (p->borders->off[i] < p->borders->off[i+1])
                    return false;
        }

        // now re-label IDs
        for(int i = 0; i < p->borders->count; i++) {
            int oldT = p->borders->tslice[i].task;
            assert((oldT >= 0) && (oldT < oldg->size));
            int newT = g->fromParent[oldT];
            assert((newT >= 0) && (newT < g->size));
            p->borders->tslice[i].task = newT;
        }
        p->borders->tasks = g->size;

        updateBorderArrayOffsets(p->borders);
    }
    laik_removeGroupUser(oldg, p);
    laik_addGroupUser(g, p);
    p->group = g;

    // make partitioning users (data containers) migrate to new group
    Laik_Data* d = p->firstPartitioningUser;
    while(d) {
        assert(d->group == oldg);
        d->group = g;
        d = d->nextPartitioningUser;
    }

    return true;
}


//----------------------------------
// Predefined Partitioners

// Block partitioner

// forward decl
void runBlockPartitioner(Laik_Partitioner* bp, Laik_BorderArray* ba);

Laik_Partitioner* laik_newBlockPartitioner(Laik_Partitioning* p)
{
    Laik_BlockPartitioner* bp;
    bp = (Laik_BlockPartitioner*) malloc(sizeof(Laik_BlockPartitioner));

    bp->base.type = LAIK_PT_Block;
    bp->base.partitioning = p;
    bp->base.run = runBlockPartitioner;

    bp->cycles = 1;
    bp->getIdxW = 0;
    bp->idxUserData = 0;
    bp->getTaskW = 0;
    bp->taskUserData = 0;

    return (Laik_Partitioner*) bp;
}

void laik_set_index_weight(Laik_Partitioning* p, Laik_GetIdxWeight_t f,
                           void* userData)
{
    assert(p->type == LAIK_PT_Block);
    Laik_BlockPartitioner* bp;
    // may create block partitioner object if not existing yet
    bp = (Laik_BlockPartitioner*) laik_get_partitioner(p);

    bp->getIdxW = f;
    bp->idxUserData = userData;

    // borders have to be recalculated
    p->bordersValid = false;
}

void laik_set_task_weight(Laik_Partitioning* p, Laik_GetTaskWeight_t f,
                          void* userData)
{
    assert(p->type == LAIK_PT_Block);
    Laik_BlockPartitioner* bp;
    // may create block partitioner object if not existing yet
    bp = (Laik_BlockPartitioner*) laik_get_partitioner(p);

    bp->getTaskW = f;
    bp->taskUserData = userData;

    // borders have to be recalculated
    p->bordersValid = false;
}

void laik_set_cycle_count(Laik_Partitioning* p, int cycles)
{
    assert(p->type == LAIK_PT_Block);
    Laik_BlockPartitioner* bp;
    // may create block partitioner object if not existing yet
    bp = (Laik_BlockPartitioner*) laik_get_partitioner(p);

    if ((cycles < 0) || (cycles>10)) cycles = 1;
    bp->cycles = cycles;

    // borders have to be recalculated
    p->bordersValid = false;
}

void runBlockPartitioner(Laik_Partitioner* pr, Laik_BorderArray* ba)
{
    Laik_BlockPartitioner* bp = (Laik_BlockPartitioner*) pr;
    Laik_Partitioning* p = bp->base.partitioning;
    assert(p->borders != 0);
    assert(p->type == LAIK_PT_Block);

    Laik_Space* s = p->space;
    Laik_Slice slc;
    setIndex(&(slc.from), 0, 0, 0);
    setIndex(&(slc.to), s->size[0], s->size[1], s->size[2]);

    int count = p->group->size;
    int pdim = p->pdim;
    uint64_t size = s->size[pdim];

    Laik_Index idx;
    double totalW;
    if (bp->getIdxW) {
        // element-wise weighting
        totalW = 0.0;
        setIndex(&idx, 0, 0, 0);
        for(uint64_t i = 0; i < size; i++) {
            idx.i[pdim] = i;
            totalW += (bp->getIdxW)(&idx, bp->idxUserData);
        }
    }
    else {
        // without weighting function, use weight 1 for every index
        totalW = (double) size;
    }

    double totalTW = 0.0;
    if (bp->getTaskW) {
        // task-wise weighting
        totalTW = 0.0;
        for(int task = 0; task < count; task++)
            totalTW += (bp->getTaskW)(task, bp->taskUserData);
    }
    else {
        // without task weighting function, use weight 1 for every task
        totalTW = (double) count;
    }

    double perPart = totalW / count / bp->cycles;
    double w = -0.5;
    int task = 0;
    int cycle = 0;

    // taskW is a correction factor, which is 1.0 without task weights
    double taskW;
    if (bp->getTaskW)
        taskW = (bp->getTaskW)(task, bp->taskUserData)
                * ((double) count) / totalTW;
    else
        taskW = 1.0;

    slc.from.i[pdim] = 0;
    for(uint64_t i = 0; i < size; i++) {
        if (bp->getIdxW) {
            idx.i[pdim] = i;
            w += (bp->getIdxW)(&idx, bp->idxUserData);
        }
        else
            w += 1.0;

        while (w >= perPart * taskW) {
            w = w - (perPart * taskW);
            if ((task+1 == count) && (cycle+1 == bp->cycles)) break;
            slc.to.i[pdim] = i;
            if (slc.from.i[pdim] < slc.to.i[pdim])
                appendSlice(ba, task, &slc);
            task++;
            if (task == count) {
                task = 0;
                cycle++;
            }
            // update taskW
            if (bp->getTaskW)
                taskW = (bp->getTaskW)(task, bp->taskUserData)
                        * ((double) count) / totalTW;
            else
                taskW = 1.0;

            // start new slice
            slc.from.i[pdim] = i;
        }
        if ((task+1 == count) && (cycle+1 == bp->cycles)) break;
    }
    assert((task+1 == count) && (cycle+1 == bp->cycles));
    slc.to.i[pdim] = size;
    appendSlice(ba, task, &slc);
}


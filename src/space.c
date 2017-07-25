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

// is slice within space borders?
bool laik_slice_isWithin(Laik_Slice* slc, Laik_Space* sp)
{
    if (slc->from.i[0] < slc->to.i[0]) {
        // not empty
        if (slc->to.i[0] > sp->size[0]) return false;
    }
    if (sp->dims == 1) return true;

    if (slc->from.i[1] < slc->to.i[1]) {
        // not empty
        if (slc->to.i[1] > sp->size[1]) return false;
    }
    if (sp->dims == 2) return true;

    if (slc->from.i[2] < slc->to.i[2]) {
        // not empty
        if (slc->to.i[2] > sp->size[2]) return false;
    }
    return true;
}

// are the slices equal?
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
int getReductionStr(char* s, Laik_ReductionOperation op)
{
    switch(op) {
    case LAIK_RO_None: return sprintf(s, "none");
    case LAIK_RO_Sum:  return sprintf(s, "sum");
    default: assert(0);
    }
    return 0;
}


int laik_getDataFlowStr(char* s, Laik_DataFlow flow)
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


int laik_getTransitionStr(char* s, Laik_Transition* t)
{
    if (t == 0)
        return 0;

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

Laik_BorderArray* allocBorders(Laik_Group* g, Laik_Space* s, int capacity)
{
    Laik_BorderArray* a;

    a = (Laik_BorderArray*) malloc(sizeof(Laik_BorderArray));
    a->off = (int*) malloc((g->size + 1) * sizeof(int));
    a->tslice = (Laik_TaskSlice*) malloc(capacity *
                                         sizeof(Laik_TaskSlice));
    a->group = g;
    a->space = s;
    a->capacity = capacity;
    a->count = 0;

    return a;
}

void appendSlice(Laik_BorderArray* a, int task, Laik_Slice* s)
{
    assert(a->count < a->capacity);
    assert((task >= 0) && (task < a->group->size));
    assert(laik_slice_isWithin(s, a->space));

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

// update offset array from slices
void updateBorderArrayOffsets(Laik_BorderArray* ba)
{
    // make sure slices are sorted according by task IDs
    qsort( &(ba->tslice[0]), ba->count,
            sizeof(Laik_TaskSlice), ts_cmp);

    int task, o = 0;
    for(task = 0; task < ba->group->size; task++) {
        ba->off[task] = o;
        while(o < ba->count) {
            if (ba->tslice[o].task > task) break;
            assert(ba->tslice[o].task == task);
            o++;
        }
    }
    ba->off[task] = o;
    assert(o == ba->count);
}


void clearBorderArray(Laik_BorderArray* ba)
{
    // to remove all entries, it's enough to set count to 0
    ba->count = 0;
}

void freeBorderArray(Laik_BorderArray* ba)
{
    free(ba->off);
    free(ba->tslice);
    free(ba);
}

// do borders cover complete space in all tasks?
// assumption: task slices sorted according to task ID
bool bordersIsAll(Laik_BorderArray* ba)
{
    if (ba->count != ba->group->size) return false;
    Laik_Slice* slc = sliceFromSpace(ba->space);
    for(int i = 0; i < ba->count; i++) {
        if (ba->tslice[i].task != i) return false;
        if (!laik_slice_isEqual(ba->space->dims, &(ba->tslice[i].s), slc))
            return false;
    }
    return true;
}

// do borders cover complete space exactly in one task?
// return -1 if no, else task ID
int bordersIsSingle(Laik_BorderArray* ba)
{
    Laik_Slice* slc = sliceFromSpace(ba->space);
    if (ba->count != 1) return -1;
    if (!laik_slice_isEqual(ba->space->dims, &(ba->tslice[0].s), slc))
        return -1;

    return ba->tslice[0].task;
}

// check if borders cover full space
static
bool coversSpace(Laik_BorderArray* ba)
{
    // TODO: only 1 dim for now
    assert(ba->space->dims == 1);

    assert(ba->count > 0);
    uint64_t min = ba->tslice[0].s.from.i[0];
    uint64_t max = ba->tslice[0].s.to.i[0];
    for(int b = 1; b < ba->count; b++) {
        if (min > ba->tslice[b].s.from.i[0])
            min = ba->tslice[b].s.from.i[0];
        if (max < ba->tslice[b].s.to.i[0])
            max = ba->tslice[b].s.to.i[0];
    }

    if ((min == 0) && (max == ba->space->size[0]))
        return true;
    return false;
}

int laik_getBorderArrayStr(char* s, Laik_BorderArray* ba)
{
    int o;

    if (!ba)
        return sprintf(s, "(no borders)");

    o = sprintf(s, "borders (%d slices in %d tasks on ",
                ba->count, ba->group->size);
    o += getSpaceStr(s+o, ba->space);
    o += sprintf(s+o,"): ");
    for(int i = 0; i < ba->count; i++) {
        if (i>0)
            o += sprintf(s+o, ", ");
        o += sprintf(s+o, "%d:", ba->tslice[i].task);
        o += getSliceStr(s+o, ba->space->dims, &(ba->tslice[i].s));
    }

    return o;
}




//-----------------------
// Laik_Partitioning


// create a new partitioning on a space
Laik_Partitioning* laik_new_partitioning(Laik_Group* g, Laik_Space* s,
                                         Laik_Partitioner* pr)
{
    Laik_Partitioning* p;
    p = (Laik_Partitioning*) malloc(sizeof(Laik_Partitioning));

    p->id = part_id++;
    p->name = strdup("partng-0     ");
    sprintf(p->name, "partng-%d", p->id);

    assert(g->inst == s->inst);
    p->group = g;
    p->space = s;

    p->partitioner = pr;

    p->bordersValid = false;
    p->borders = 0;

    p->nextSpaceUser = 0;
    p->nextGroupUser = 0;
    p->firstPartitioningUser = 0;

    laik_addSpaceUser(s, p);
    laik_addGroupUser(p->group, p);

    if (laik_logshown(1)) {
        laik_log(1, "new partitioning '%s': space '%s', "
                 "group %d (size %d, myid %d), partitioner '%s'\n",
                 p->name, s->name,
                 p->group->gid, p->group->size, p->group->myid,
                 pr->name);
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


void laik_set_partitioner(Laik_Partitioning* p, Laik_Partitioner* pr)
{
    assert(pr->run != 0);
    p->partitioner = pr;
}

Laik_Partitioner* laik_get_partitioner(Laik_Partitioning* p)
{
    return p->partitioner;
}

Laik_Space* laik_get_pspace(Laik_Partitioning* p)
{
    return p->space;
}

Laik_Group* laik_get_pgroup(Laik_Partitioning* p)
{
    return p->group;
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
    if (!p->bordersValid)
        laik_calc_partitioning(p);

    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group
    assert(myid < p->group->size);
    return p->borders->off[myid+1] - p->borders->off[myid];
}

// get slice number <n> from the slices of this task
Laik_Slice* laik_my_slice(Laik_Partitioning* p, int n)
{
    if (!p->bordersValid)
        laik_calc_partitioning(p);

    int myid = p->group->myid;
    int o = p->borders->off[myid] + n;
    if (o >= p->borders->off[myid+1]) {
        // slice <n> is invalid
        return 0;
    }
    assert(p->borders->tslice[o].task == myid);
    return &(p->borders->tslice[o].s);
}

Laik_Slice* laik_my_slice1(Laik_Partitioning* p, int n,
                           uint64_t* from, uint64_t* to)
{
    assert(p->space->dims == 1);
    Laik_Slice* s = laik_my_slice(p, n);
    if (from) *from = s ? s->from.i[0] : 0;
    if (to) *to = s ? s->to.i[0] : 0;

    return s;
}


// give a partitioning a name, for debug output
void laik_set_partitioning_name(Laik_Partitioning* p, char* n)
{
    p->name = strdup(n);
}



// run a partitioner, returning newly calculated borders
// the partitioner may use old borders from <oldBA>
Laik_BorderArray* laik_run_partitioner(Laik_Partitioner* pr,
                                       Laik_Group* g, Laik_Space* space,
                                       Laik_BorderArray* oldBA)
{
    Laik_BorderArray* ba;

    ba = allocBorders(g, space, 2 * g->size);
    if (oldBA) {
        assert(oldBA->group == g);
        assert(oldBA->space == space);
    }
    (pr->run)(pr, ba, oldBA);
    updateBorderArrayOffsets(ba);

    if (!coversSpace(ba))
        laik_log(LAIK_LL_Panic, "borders do not cover space");

    if (laik_logshown(1)) {
        char s[1000];
        int o;
        o = sprintf(s, "run partitioner '%s' (group %d, myid %d, space '%s'):\n",
                    pr->name, g->gid, g->myid, space->name);
        o += sprintf(s+o, " old: ");
        o += laik_getBorderArrayStr(s+o, oldBA);
        o += sprintf(s+o, "\n new: ");
        o += laik_getBorderArrayStr(s+o, ba);
        laik_log(1, "%s\n", s);
    }

    return ba;
}


// set new partitioning borders
void laik_set_borders(Laik_Partitioning* p, Laik_BorderArray* ba)
{
    assert(p->group == ba->group);
    assert(p->space == ba->space);

    if (laik_logshown(1)) {
        char s[1000];
        int o;
        o = sprintf(s, "setting borders (part '%s', group %d, myid %d):\n ",
                    p->name, ba->group->gid, ba->group->myid);
        o += laik_getBorderArrayStr(s+o, ba);
        laik_log(1, "%s\n", s);
    }

    // visit all users of this partitioning
    Laik_Data* d = p->firstPartitioningUser;
    while(d) {
        laik_switchto_borders(d, ba);
        d = d->nextPartitioningUser;
    }

    if (p->borders)
        freeBorderArray(p->borders);
    p->borders = ba;
    p->bordersValid = true;
}

// return currently set borders in partitioning
Laik_BorderArray* laik_get_borders(Laik_Partitioning* p)
{
    if (p->bordersValid) {
        assert(p->borders);
        return p->borders;
    }
    return 0;
}

// calculate partition borders, overwriting old
void laik_calc_partitioning(Laik_Partitioning* p)
{
    Laik_BorderArray* ba;

    assert(p->partitioner);
    ba = laik_run_partitioner(p->partitioner,
                              p->group, p->space, 0);

    laik_set_borders(p, ba);
}



// append a partitioning to a partioning group whose consistency should
// be enforced at the same point in time
void laik_append_partitioning(Laik_PartGroup* g, Laik_Partitioning* p)
{
    assert(0); // TODO
}

// Calculate communication required for transitioning between partitionings
Laik_Transition*
laik_calc_transition(Laik_Group* group, Laik_Space* space,
                     Laik_BorderArray* fromBA, Laik_DataFlow fromFlow,
                     Laik_BorderArray* toBA, Laik_DataFlow toFlow)
{
    // no action if not part of the group
    if (group->myid == -1)
        return 0;

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

    // make sure requested operation is consistent
    if (fromBA == 0) {
        // start: we come from nothing, go to initial partitioning
        assert(toBA != 0);
        assert(!laik_do_copyin(toFlow));
        assert(toBA->group == group);
        assert(toBA->space == space);
    }
    else if (toBA == 0) {
        // end: go to nothing
        assert(fromBA != 0);
        assert(!laik_do_copyout(fromFlow));
        assert(fromBA->group == group);
        assert(fromBA->space == space);
    }
    else {
        // to and from set
        if (laik_do_copyin(toFlow)) {
            // values must come from something
            assert(laik_do_copyout(fromFlow) ||
                   laik_is_reduction(fromFlow));
        }
        assert(fromBA->group == group);
        assert(fromBA->space == space);
        assert(toBA->group == group);
        assert(toBA->space == space);
    }

    int dims = space->dims;
    int myid = group->myid;
    int count = group->size;

    // init values as next phase does a reduction?
    if ((toBA != 0) && laik_do_init(toFlow)) {

        for(int o = toBA->off[myid]; o < toBA->off[myid+1]; o++) {
            if (laik_slice_isEmpty(dims, &(toBA->tslice[o].s))) continue;
            assert(initCount < TRANSSLICES_MAX);
            struct initTOp* op = &(init[initCount]);
            op->slc = toBA->tslice[o].s;
            op->sliceNo = o - toBA->off[myid];
            op->redOp = laik_get_reduction(toFlow);
            assert(op->redOp != LAIK_RO_None);
            initCount++;
        }
    }

    if ((fromBA != 0) && (toBA != 0)) {

        // determine local slices to keep
        // (may need local copy if from/to mappings are different).
        // reductions are not handled here, but by backend
        if (laik_do_copyout(fromFlow) && laik_do_copyin(toFlow)) {
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
        if (laik_is_reduction(fromFlow)) {
            // reductions always should involve everyone
            assert(bordersIsAll(fromBA));
            if (laik_do_copyin(toFlow)) {
                assert(redCount < TRANSSLICES_MAX);
                // reduction result either goes to all or master
                int root = bordersIsSingle(toBA);
                assert(bordersIsAll(toBA) || (root == 0));

                struct redTOp* op = &(red[redCount]);
                op->slc = *sliceFromSpace(space); // complete space
                op->redOp = laik_get_reduction(fromFlow);
                op->rootTask = (root >= 0) ? root : -1;

                redCount++;
            }
        }

        // something to send?
        if (laik_do_copyout(fromFlow)) {
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
        if (!laik_is_reduction(fromFlow) && laik_do_copyin(toFlow)) {
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


// couple different LAIK instances via spaces:
// one partition of calling task in outer space is mapped to inner space
void laik_couple_nested(Laik_Space* outer, Laik_Space* inner)
{
    assert(0); // TODO
}

// migrate borders to new group without changing borders
// - added tasks get empty partitions
// - removed tasks must have empty partitiongs
void laik_migrate_borders(Laik_BorderArray* ba, Laik_Group* newg)
{
    Laik_Group* oldg = ba->group;
    int* fromOld; // mapping of IDs from old group to new group

    if (newg->parent == oldg) {
        // new group is child of old
        fromOld = newg->fromParent;
    }
    else if (oldg->parent == newg) {
        // new group is parent of old
        fromOld = oldg->toParent;
    }
    else {
        // other cases not supported
        assert(0);
    }

    // check that partitions of tasks to be removed are empty
    for(int i = 0; i < oldg->size; i++) {
        if (fromOld[i] < 0)
            assert(ba->off[i] == ba->off[i+1]);
    }

    // update slice IDs
    for(int i = 0; i < ba->count; i++) {
        int oldT = ba->tslice[i].task;
        assert((oldT >= 0) && (oldT < oldg->size));
        int newT = fromOld[oldT];
        assert((newT >= 0) && (newT < newg->size));
        ba->tslice[i].task = newT;
    }

    // resize offset array if needed
    if (newg->size > oldg->size) {
        free(ba->off);
        ba->off = (int*) malloc((newg->size +1) * sizeof(int));
    }
    ba->group = newg;
    updateBorderArrayOffsets(ba);
}

// migrate a partitioning defined on one task group to another group
bool laik_migrate_partitioning(Laik_Partitioning* p,
                               Laik_Group* newg)
{
    Laik_Group* oldg = p->group;
    assert(oldg != newg);

    if (p->bordersValid) {
        assert(p->borders && (p->borders->group == oldg));
        laik_migrate_borders(p->borders, newg);
    }

    laik_removeGroupUser(oldg, p);
    laik_addGroupUser(newg, p);
    p->group = newg;

    // make partitioning users (data containers) migrate to new group
    Laik_Data* d = p->firstPartitioningUser;
    while(d) {
        assert(d->group == oldg);
        d->group = newg;
        d = d->nextPartitioningUser;
    }

    return true;
}


//----------------------------------
// Built-in partitioners

static bool space_init_done = false;
Laik_Partitioner* laik_All = 0;
Laik_Partitioner* laik_Master = 0;

void laik_space_init()
{
    if (space_init_done) return;

    laik_All    = laik_new_all_partitioner();
    laik_Master = laik_new_master_partitioner();

    space_init_done = true;
}

Laik_Partitioner* laik_new_partitioner(char* name,
                                       laik_run_partitioner_t f, void* d)
{
    Laik_Partitioner* pr;
    pr = (Laik_Partitioner*) malloc(sizeof(Laik_Partitioner));

    pr->name = name;
    pr->run = f;
    pr->data = d;

    return pr;
}


// Simple partitioners

// all-partitioner: all tasks have access to all indexes

void runAllPartitioner(Laik_Partitioner* pr,
                       Laik_BorderArray* ba, Laik_BorderArray* oldBA)
{
    Laik_Slice slc;
    Laik_Space* s = ba->space;
    Laik_Group* g = ba->group;

    for(int task = 0; task < g->size; task++) {
        setIndex(&(slc.from), 0, 0, 0);
        setIndex(&(slc.to), s->size[0], s->size[1], s->size[2]);
        appendSlice(ba, task, &slc);
    }
}

Laik_Partitioner* laik_new_all_partitioner()
{
    return laik_new_partitioner("all", runAllPartitioner, 0);
}

// master-partitioner: only task 0 has access to all indexes

void runMasterPartitioner(Laik_Partitioner* pr,
                          Laik_BorderArray* ba, Laik_BorderArray* oldBA)
{
    Laik_Slice slc;
    Laik_Space* s = ba->space;

    // only full slice for master
    setIndex(&(slc.from), 0, 0, 0);
    setIndex(&(slc.to), s->size[0], s->size[1], s->size[2]);
    appendSlice(ba, 0, &slc);
}

Laik_Partitioner* laik_new_master_partitioner()
{
    return laik_new_partitioner("master", runMasterPartitioner, 0);
}

// copy-partitioner: copy the borders from another partitioning
//
// we assume 1d partitioning on spaces with multiple dimensions.
// Thus, parameters is not only the base partitioning, but also the
// dimension of borders to copy from one to the other partitioning

void runCopyPartitioner(Laik_Partitioner* pr,
                        Laik_BorderArray* ba, Laik_BorderArray* oldBA)
{
    Laik_Slice slc;
    Laik_Space* s = ba->space;
    Laik_CopyPartitionerData* data = (Laik_CopyPartitionerData*) pr->data;
    assert(data);

    Laik_Partitioning* base = data->base;
    int fromDim = data->fromDim;
    int toDim = data->toDim;

    assert(base);
    assert(base->bordersValid);
    assert(base->group == ba->group); // base must use same task group
    assert((fromDim >= 0) && (fromDim < base->space->dims));
    assert((toDim >= 0) && (toDim < s->dims));

    Laik_BorderArray* baseBorders = base->borders;
    assert(baseBorders);

    for(int i = 0; i < baseBorders->count; i++) {
        setIndex(&(slc.from), 0, 0, 0);
        setIndex(&(slc.to), s->size[0], s->size[1], s->size[2]);
        slc.from.i[toDim] = baseBorders->tslice[i].s.from.i[fromDim];
        slc.to.i[toDim] = baseBorders->tslice[i].s.to.i[fromDim];
        appendSlice(ba, baseBorders->tslice[i].task, &slc);
    }
}

Laik_Partitioner* laik_new_copy_partitioner(Laik_Partitioning* base,
                                            int fromDim, int toDim)
{
    Laik_CopyPartitionerData* data;
    int dsize = sizeof(Laik_CopyPartitionerData);
    data = (Laik_CopyPartitionerData*) malloc(dsize);

    data->base = base;
    data->fromDim = fromDim;
    data->toDim = toDim;

    return laik_new_partitioner("copy", runCopyPartitioner, data);
}


// block partitioner: split one dimension of space into blocks
//
// this partitioner supports:
// - index-wise weighting: give each task indexes with similar weight sum
// - task-wise weighting: scaling factor, allowing load-balancing
//
// when distributing indexes, a given number of rounds is done over tasks,
// defaulting to 1 (see cycle parameter).

void runBlockPartitioner(Laik_Partitioner* pr,
                         Laik_BorderArray* ba, Laik_BorderArray* oldBA)
{
    Laik_BlockPartitionerData* data;
    data = (Laik_BlockPartitionerData*) pr->data;

    Laik_Space* s = ba->space;
    Laik_Slice slc;
    setIndex(&(slc.from), 0, 0, 0);
    setIndex(&(slc.to), s->size[0], s->size[1], s->size[2]);

    int count = ba->group->size;
    int pdim = data->pdim;
    uint64_t size = s->size[pdim];

    Laik_Index idx;
    double totalW;
    if (data && data->getIdxW) {
        // element-wise weighting
        totalW = 0.0;
        setIndex(&idx, 0, 0, 0);
        for(uint64_t i = 0; i < size; i++) {
            idx.i[pdim] = i;
            totalW += (data->getIdxW)(&idx, data->userData);
        }
    }
    else {
        // without weighting function, use weight 1 for every index
        totalW = (double) size;
    }

    double totalTW = 0.0;
    if (data && data->getTaskW) {
        // task-wise weighting
        totalTW = 0.0;
        for(int task = 0; task < count; task++)
            totalTW += (data->getTaskW)(task, data->userData);
    }
    else {
        // without task weighting function, use weight 1 for every task
        totalTW = (double) count;
    }

    int cycles = data ? data->cycles : 1;
    double perPart = totalW / count / cycles;
    double w = -0.5;
    int task = 0;
    int cycle = 0;

    // taskW is a correction factor, which is 1.0 without task weights
    double taskW;
    if (data && data->getTaskW)
        taskW = (data->getTaskW)(task, data->userData)
                * ((double) count) / totalTW;
    else
        taskW = 1.0;

    slc.from.i[pdim] = 0;
    for(uint64_t i = 0; i < size; i++) {
        if (data && data->getIdxW) {
            idx.i[pdim] = i;
            w += (data->getIdxW)(&idx, data->userData);
        }
        else
            w += 1.0;

        while (w >= perPart * taskW) {
            w = w - (perPart * taskW);
            if ((task+1 == count) && (cycle+1 == cycles)) break;
            slc.to.i[pdim] = i;
            if (slc.from.i[pdim] < slc.to.i[pdim])
                appendSlice(ba, task, &slc);
            task++;
            if (task == count) {
                task = 0;
                cycle++;
            }
            // update taskW
            if (data && data->getTaskW)
                taskW = (data->getTaskW)(task, data->userData)
                        * ((double) count) / totalTW;
            else
                taskW = 1.0;

            // start new slice
            slc.from.i[pdim] = i;
        }
        if ((task+1 == count) && (cycle+1 == cycles)) break;
    }
    assert((task+1 == count) && (cycle+1 == cycles));
    slc.to.i[pdim] = size;
    appendSlice(ba, task, &slc);
}


Laik_Partitioner* laik_new_block_partitioner(int pdim, int cycles,
                                             Laik_GetIdxWeight_t ifunc,
                                             Laik_GetTaskWeight_t tfunc,
                                             void* userData)
{
    Laik_BlockPartitionerData* data;
    int dsize = sizeof(Laik_BlockPartitionerData);
    data = (Laik_BlockPartitionerData*) malloc(dsize);

    data->pdim = pdim;
    data->cycles = cycles;
    data->getIdxW = ifunc;
    data->userData = userData;
    data->getTaskW = tfunc;

    return laik_new_partitioner("block", runBlockPartitioner, data);
}

Laik_Partitioner* laik_new_block_partitioner1()
{
    return laik_new_block_partitioner(0, 1, 0, 0, 0);
}

Laik_Partitioner* laik_new_block_partitioner_iw1(Laik_GetIdxWeight_t f,
                                                      void* userData)
{
    return laik_new_block_partitioner(0, 1, f, 0, userData);
}

Laik_Partitioner* laik_new_block_partitioner_tw1(Laik_GetTaskWeight_t f,
                                                      void* userData)
{
    return laik_new_block_partitioner(0, 1, 0, f, userData);
}

void laik_set_index_weight(Laik_Partitioner* pr, Laik_GetIdxWeight_t f,
                           void* userData)
{
    Laik_BlockPartitionerData* data;
    data = (Laik_BlockPartitionerData*) pr->data;

    data->getIdxW = f;
    data->userData = userData;
}

void laik_set_task_weight(Laik_Partitioner* pr, Laik_GetTaskWeight_t f,
                          void* userData)
{
    Laik_BlockPartitionerData* data;
    data = (Laik_BlockPartitionerData*) pr->data;

    data->getTaskW = f;
    data->userData = userData;
}

void laik_set_cycle_count(Laik_Partitioner* pr, int cycles)
{
    Laik_BlockPartitionerData* data;
    data = (Laik_BlockPartitionerData*) pr->data;

    if ((cycles < 0) || (cycles>10)) cycles = 1;
    data->cycles = cycles;
}



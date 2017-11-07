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
static int part_id = 0;

// helpers

void laik_set_index(Laik_Index* i, uint64_t i1, uint64_t i2, uint64_t i3)
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

void laik_sub_index(Laik_Index* res, Laik_Index* src1, Laik_Index* src2)
{
    res->i[0] = src1->i[0] - src2->i[0];
    res->i[1] = src1->i[1] - src2->i[1];
    res->i[2] = src1->i[2] - src2->i[2];
}


bool laik_index_isEqual(int dims, Laik_Index* i1, Laik_Index* i2)
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
bool laik_slice_within_slice(int dims, Laik_Slice* slc1, Laik_Slice* slc2)
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
bool laik_slice_within_space(Laik_Slice* slc, Laik_Space* sp)
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
uint64_t laik_slice_size(int dims, Laik_Slice* s)
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
const Laik_Slice* laik_space_getslice(Laik_Space* space)
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

    space->id = space_id++;
    space->name = strdup("space-0     ");
    sprintf(space->name, "space-%d", space->id);

    space->inst = inst;
    space->dims = 0; // invalid
    space->firstPartitioningForSpace = 0;
    space->nextSpaceForInstance = 0;

    // append this space to list of spaces used by LAIK instance
    laik_addSpaceForInstance(inst, space);

    return space;
}

// create a new index space object with an initial size
Laik_Space* laik_new_space_1d(Laik_Instance* i, uint64_t s1)
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

Laik_Space* laik_new_space_2d(Laik_Instance* i,
                              uint64_t s1, uint64_t s2)
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
                              uint64_t s1, uint64_t s2, uint64_t s3)
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
void laik_change_space_1d(Laik_Space* s, uint64_t from1, uint64_t to1)
{
    assert(s->dims == 1);
    if ((s->s.from.i[0] == from1) && (s->s.to.i[0] == to1))
        return;

    s->s.from.i[0] = from1;
    s->s.to.i[0] = to1;

    // TODO: notify partitionings about space change
}



void laik_addPartitioningForSpace(Laik_Space* s, Laik_Partitioning* p)
{
    assert(p->nextPartitioningForSpace == 0);
    p->nextPartitioningForSpace = s->firstPartitioningForSpace;
    s->firstPartitioningForSpace = p;
}

void laik_removePartitioningFromSpace(Laik_Space* s, Laik_Partitioning* p)
{
    if (s->firstPartitioningForSpace == p) {
        s->firstPartitioningForSpace = p->nextPartitioningForSpace;
    }
    else {
        // search for previous item
        Laik_Partitioning* pp = s->firstPartitioningForSpace;
        while(pp->nextPartitioningForSpace != p)
            pp = pp->nextPartitioningForSpace;
        assert(pp != 0); // not found, should not happen
        pp->nextPartitioningForSpace = p->nextPartitioningForSpace;
    }
    p->nextPartitioningForSpace = 0;
}



//-----------------------
// Laik_BorderArray

Laik_BorderArray* laik_allocBorders(Laik_Group* g, Laik_Space* s)
{
    Laik_BorderArray* a;

    a = malloc(sizeof(Laik_BorderArray));
    a->off = malloc(sizeof(int) * (g->size + 1));

    a->tslice = 0;
    a->tss1d = 0;

    a->group = g;
    a->space = s;
    a->capacity = 0;
    a->count = 0;

    return a;
}

// called by partitioners
Laik_TaskSlice* laik_append_slice(Laik_BorderArray* a, int task, Laik_Slice* s,
                                  int tag, void* data)
{
    if (a->count == a->capacity) {
        assert(a->tss1d == 0);
        if (a->tslice == 0) {
            a->capacity = 4;
            a->tslice = malloc(sizeof(Laik_TaskSlice_Gen) * a->capacity);
        }
        else {
            a->capacity = a->capacity *2;
            a->tslice = realloc(a->tslice,
                                sizeof(Laik_TaskSlice_Gen) * a->capacity);
        }
    }
    assert((task >= 0) && (task < a->group->size));
    assert(laik_slice_within_space(s, a->space));
    assert(a->tslice);

    Laik_TaskSlice_Gen* ts = &(a->tslice[a->count]);
    a->count++;

    ts->type = TS_Generic;
    ts->task = task;
    ts->s = *s;
    ts->tag = tag;
    ts->data = data;
    ts->mapNo = 0;

    return (Laik_TaskSlice*) ts;
}

// append 1d single-index slice
Laik_TaskSlice* laik_append_index_1d(Laik_BorderArray* a, int task, uint64_t idx)
{
    if (a->count == a->capacity) {
        assert(a->tslice == 0);
        assert(a->space->dims == 1);
        if (a->tss1d == 0) {
            a->capacity = 4;
            a->tss1d = malloc(sizeof(Laik_TaskSlice_Single1d) * a->capacity);
        }
        else {
            a->capacity = a->capacity *2;
            a->tss1d = realloc(a->tslice,
                               sizeof(Laik_TaskSlice_Single1d) * a->capacity);
        }
    }
    assert((task >= 0) && (task < a->group->size));
    assert((idx >= a->space->s.from.i[0]) && (idx < a->space->s.to.i[0]));
    assert(a->tss1d);

    Laik_TaskSlice_Single1d* ts = &(a->tss1d[a->count]);
    a->count++;

    ts->type = TS_Single1d;
    ts->task = task;
    ts->idx = idx;

    return (Laik_TaskSlice*) ts;
}


// helpers for slice merging

static
void laik_remove_slice(Laik_BorderArray* ba, int task, int index)
{
    assert(ba->space->dims == 1); // not yet implemented for higher dimensions
    assert((task >= 0) && (task < ba->group->size));
    assert(index < ba->count);

    for(int i = index; i < ba->count - 1; i++) {
        ba->tslice[i] = ba->tslice[i + 1];
    }
    ba->count--;
}

// TODO extend for 2d and 3d
static
bool laik_are_slices_mergeable(const Laik_Slice* s1, const Laik_Slice* s2)
{
    if ((s1->from.i[0] <= s2->from.i[0]) && (s2->from.i[0] <= s1->to.i[0]))
        return true;

    if ((s2->from.i[0] <= s1->from.i[0]) && (s1->from.i[0] <= s2->to.i[0]))
        return true;

    return false;
}

static
void laik_merge_slices(Laik_Slice* s1, const Laik_Slice* s2,
                       Laik_BorderArray* ba, int task, int index)
{
    assert(ba->space->dims == 1); // not yet implemented for higher dimensions

    s1->from.i[0] = MIN ( s1->from.i[0] , s2->from.i[0] );
    s1->to.i[0] = MAX ( s1->to.i[0] , s2->to.i[0] );
    laik_remove_slice(ba, task, index);
}

Laik_Space* laik_borderarray_getspace(Laik_BorderArray* ba)
{
    return ba->space;
}

Laik_Group* laik_borderarray_getgroup(Laik_BorderArray* ba)
{
    return ba->group;
}

int laik_borderarray_getcount(Laik_BorderArray* ba)
{
    return ba->count;
}

Laik_TaskSlice* laik_borderarray_get_tslice(Laik_BorderArray* ba, int n)
{
    assert(n < ba->count);
    if (ba->tslice)
        return (Laik_TaskSlice*) &(ba->tslice[n]);
    if (ba->tss1d)
        return (Laik_TaskSlice*) &(ba->tss1d[n]);
    assert(0);
    return 0;
}

const Laik_Slice* laik_taskslice_getslice(Laik_TaskSlice* ts)
{
    static Laik_Slice slc;

    if (!ts) return 0;
    switch(ts->type) {
    case TS_Generic:
        return &(((Laik_TaskSlice_Gen*)ts)->s);
    case TS_Single1d:
        slc.from.i[0] = ((Laik_TaskSlice_Single1d*)ts)->idx;
        slc.to.i[0] = slc.from.i[0] + 1;
        return &slc;
    default:
        assert(0);
    }
    return 0;
}

int laik_taskslice_gettask(Laik_TaskSlice* ts)
{
    return ts->task;
}

// get a custom data pointer from the partitioner
void* laik_partitioner_data(Laik_Partitioner* partitioner)
{
    return partitioner->data;
}

//TODO extend to 2d and 3d
static
void removeBorderArrayMergeSlices(Laik_BorderArray* ba)
{
    assert(ba->space->dims == 1);
    laik_log(1,"Number of slices before merging: %d\n", ba->count);

    for(int i = 0; i < ba->count; i++) {

        Laik_Slice* s1 = &(ba->tslice[i].s);
        int task1 = ba->tslice[i].task;

        for(int j = i+1; j < ba->count; j++) {
            const Laik_Slice* s2 = &(ba->tslice[j].s);
            int task2 = ba->tslice[j].task;

            if (laik_slice_intersect(1, s1, s2) && (task1 == task2)) {
                laik_log(1, "  task %d, slice %d, from:%d, to:%d\n",
                         task1, i, s1->from.i[0], s1->to.i[0] );
                laik_log(1, "  task %d, slice %d, from:%d, to:%d\n",
                         task2, j, s2->from.i[0], s2->to.i[0] );

                if (laik_are_slices_mergeable(s1, s2)) {
                    laik_log(1,"    slices %d and %d are mergeable.\n", i, j);
                    // merge them into first slice, remove slice at index j
                    laik_merge_slices(s1, s2, ba, task2, j);
                }

            }
        }
    }
    laik_log(1, "Number of slices in ba after merging: %d\n", ba->count);
}

// sort function, called after partitioner run
static
int tsgen_cmp(const void *p1, const void *p2)
{
    const Laik_TaskSlice_Gen* ts1 = (const Laik_TaskSlice_Gen*) p1;
    const Laik_TaskSlice_Gen* ts2 = (const Laik_TaskSlice_Gen*) p2;
    if (ts1->task == ts2->task) {
        // we want same tags in a row for processing in prepareMaps
        if (ts1->tag == ts2->tag) {
            // sort slices for same task by start index (not really needed)
            return ts1->s.from.i[0] - ts2->s.from.i[0];
        }
        return ts1->tag - ts2->tag;
    }
    return ts1->task - ts2->task;
}

static
int tss1d_cmp(const void *p1, const void *p2)
{
    const Laik_TaskSlice_Single1d* ts1 = (const Laik_TaskSlice_Single1d*) p1;
    const Laik_TaskSlice_Single1d* ts2 = (const Laik_TaskSlice_Single1d*) p2;
    if (ts1->task == ts2->task) {
        // sort slices for same task by index
        return ts1->idx - ts2->idx;
    }
    return ts1->task - ts2->task;
}


// (1) update offset array from slices, (2) calculate map numbers from tags
static
void updateBorderArrayOffsets(Laik_BorderArray* ba)
{
    // make sure slices are sorted according by task IDs
    qsort( &(ba->tslice[0]), ba->count,
            sizeof(Laik_TaskSlice_Gen), tsgen_cmp);

    int task, mapNo, lastTag, off;
    off = 0;
    for(task = 0; task < ba->group->size; task++) {
        ba->off[task] = off;
        mapNo = -1; // for numbering of mappings according to tags
        lastTag = -1;
        while(off < ba->count) {
            Laik_TaskSlice_Gen* ts = &(ba->tslice[off]);
            if (ts->task > task) break;
            assert(ts->task == task);
            if ((ts->tag == 0) || (ts->tag != lastTag)) {
                mapNo++;
                lastTag = ts->tag;
            }
            ts->mapNo = mapNo;
            off++;
        }
    }
    ba->off[task] = off;
    assert(off == ba->count);
}

void laik_clearBorderArray(Laik_BorderArray* ba)
{
    // to remove all entries, it's enough to set count to 0
    ba->count = 0;
}

void laik_freeBorderArray(Laik_BorderArray* ba)
{
    free(ba->off);
    free(ba->tslice);
    free(ba);
}

// do borders cover complete space in all tasks?
// assumption: task slices sorted according to task ID
static
bool bordersIsAll(Laik_BorderArray* ba)
{
    if (ba->count != ba->group->size) return false;
    for(int i = 0; i < ba->count; i++) {
        if (ba->tslice[i].task != i) return false;
        if (!laik_slice_isEqual(ba->space->dims,
                                &(ba->tslice[i].s), &(ba->space->s)))
            return false;
    }
    return true;
}

// do borders cover complete space exactly in one task?
// return -1 if no, else task ID
static
int bordersIsSingle(Laik_BorderArray* ba)
{
    if (ba->count != 1) return -1;
    if (!laik_slice_isEqual(ba->space->dims,
                            &(ba->tslice[0].s), &(ba->space->s)))
        return -1;

    return ba->tslice[0].task;
}

// Check if slices in border array cover full space;
// works for 1d/2d/3d spaces
//
// we maintain a list of slices not yet covered,
// starting with the one slice equal to full space, and then
// subtract the slices from the border array step-by-step
// from each of the not-yet-covered slices, creating a
// new list of not-yet-covered slices.
//
// Note: subtraction of a slice from another one may result in
// multiple smaller slices which are appended to the not-yet-covered
// list (eg. in 3d, 6 smaller slices may be created).

// print verbose debug output?
#define DEBUG_COVERSPACE 1

// TODO: use dynamic list
#define COVERLIST_MAX 100
static Laik_Slice notcovered[COVERLIST_MAX];
static int notcovered_count;

static void appendToNotcovered(Laik_Slice* s)
{
    assert(notcovered_count < COVERLIST_MAX);
    notcovered[notcovered_count] = *s;
    notcovered_count++;
}

#ifdef DEBUG_COVERSPACE
static void log_Notcovered(int dims, Laik_Slice* toRemove)
{
    laik_log_append("not covered: (");
    for(int j = 0; j < notcovered_count; j++) {
        if (j>0) laik_log_append(", ");
        laik_log_Slice(dims, &(notcovered[j]));
    }
    laik_log_append(")");
    if (toRemove) {
        laik_log_append("\n  removing ");
        laik_log_Slice(dims, toRemove);
    }
}
#endif

static
bool coversSpace(Laik_BorderArray* ba)
{
    int dims = ba->space->dims;
    notcovered_count = 0;

    // start with full space not-yet-covered
    appendToNotcovered(&(ba->space->s));

    // remove each slice in border array
    for(int i = 0; i < ba->count; i++) {
        Laik_Slice* toRemove = &(ba->tslice[i].s);

#ifdef DEBUG_COVERSPACE
        if (laik_log_begin(1)) {
            laik_log_append("coversSpace - ");
            log_Notcovered(dims, toRemove);
            laik_log_flush(0);
        }
#endif

        int count = notcovered_count; // number of slices to visit
        for(int j = 0; j < count; j++) {
            Laik_Slice* orig = &(notcovered[j]);

            if (laik_slice_intersect(dims, orig, toRemove) == 0) {
                // slice to remove does not overlap with orig: keep original
                appendToNotcovered(orig);
                continue;
            }

            // subtract toRemove from orig

            // check for space not covered in orig, loop through valid dims
            for(int d = 0; d < dims; d++) {
                // space in dim <d> before <toRemove> ?
                if (orig->from.i[d] < toRemove->from.i[d]) {
                    // yes, add to not-covered
                    Laik_Slice s = *orig;
                    s.to.i[d] = toRemove->from.i[d];
                    appendToNotcovered(&s);
                    // remove appended part from <orig>
                    orig->from.i[d] = toRemove->from.i[d];
                }
                // space in dim <d> after <toRemove> ?
                if (orig->to.i[d] > toRemove->to.i[d]) {
                    Laik_Slice s = *orig;
                    s.from.i[d] = toRemove->to.i[d];
                    appendToNotcovered(&s);
                    // remove appended part from <orig>
                    orig->to.i[d] = toRemove->to.i[d];
                }
            }
        }
        if (notcovered_count == count) {
            // nothing appended, ie. nothing left?
            notcovered_count = 0;
            break;
        }
        // move appended slices to start
        for(int j = 0; j < notcovered_count - count; j++)
            notcovered[j] = notcovered[count + j];
        notcovered_count = notcovered_count - count;
    }

#ifdef DEBUG_COVERSPACE
    if (laik_log_begin(1)) {
        laik_log_append("coversSpace - remaining ");
        log_Notcovered(dims, 0);
        laik_log_flush(0);
    }
#endif

    // only if no slices are left, we did cover full space
    return (notcovered_count == 0);
}



// are borders equal?
static
bool laik_border_isEqual(Laik_BorderArray* b1, Laik_BorderArray* b2)
{
    assert(b1);
    assert(b2);
    if (b1->group != b2->group) return false;
    if (b1->space != b2->space) return false;
    if (b1->count != b2->count) return false;

    for(int i = 0; i < b1->group->size; i++)
        if (b1->off[i] != b2->off[i]) return false;

    for(int i = 0; i < b1->count; i++) {
        // tasks must match, as offset array matched
        assert(b1->tslice[i].task == b2->tslice[i].task);

        if (!laik_slice_isEqual(b1->space->dims,
                                &(b1->tslice[i].s),
                                &(b2->tslice[i].s))) return false;
    }
    return true;
}




//-----------------------
// Laik_Partitioning


// create a new partitioning on a space
Laik_Partitioning*
laik_new_partitioning(Laik_Group* group, Laik_Space* space,
                      Laik_Partitioner* pr, Laik_Partitioning *base)
{
    Laik_Partitioning* p;
    p = malloc(sizeof(Laik_Partitioning));

    p->id = part_id++;
    p->name = strdup("partng-0     ");
    sprintf(p->name, "partng-%d", p->id);

    assert(group->inst == space->inst);
    p->group = group;
    p->space = space;

    p->partitioner = pr;
    p->base = base;

    p->bordersValid = false;
    p->borders = 0;

    p->nextPartitioningForSpace = 0;
    p->nextPartitioningForGroup = 0;
    p->nextPartitioningForBase  = 0;
    p->firstDataForPartitioning = 0;
    p->firstPartitioningForBase = 0;

    laik_addPartitioningForSpace(space, p);
    laik_addPartitioningForGroup(p->group, p);

    if (base) {
        assert(base->group == group);
        laik_addPartitioningForBase(base, p);
    }

    if (laik_log_begin(1)) {
        laik_log_append("new partitioning '%s':\n  space '%s', "
                        "group %d (size %d, myid %d), partitioner '%s'",
                        p->name, space->name,
                        p->group->gid, p->group->size, p->group->myid,
                        pr->name);
        if (base)
            laik_log_append(", base '%s'", base->name);
        laik_log_flush(0);
    }

    return p;
}

void laik_addPartitioningForBase(Laik_Partitioning* base,
                                 Laik_Partitioning* p)
{
    assert(p->nextPartitioningForBase == 0);
    p->nextPartitioningForBase = base->firstPartitioningForBase;
    base->firstPartitioningForBase = p;
}

void laik_removePartitioningFromBase(Laik_Partitioning* base,
                                     Laik_Partitioning* p)
{
    if (base->firstPartitioningForBase == p) {
        base->firstPartitioningForBase = p->nextPartitioningForBase;
    }
    else {
        // search for previous item
        Laik_Partitioning* pp = base->firstPartitioningForBase;
        while(pp->nextPartitioningForBase != p)
            pp = pp->nextPartitioningForBase;
        assert(pp != 0); // not found, should not happen
        pp->nextPartitioningForBase = p->nextPartitioningForBase;
    }
    p->nextPartitioningForBase = 0;
}


void laik_addDataForPartitioning(Laik_Partitioning* p, Laik_Data* d)
{
    assert(d->nextPartitioningUser == 0);
    d->nextPartitioningUser = p->firstDataForPartitioning;
    p->firstDataForPartitioning = d;
}

void laik_removeDataFromPartitioning(Laik_Partitioning* p, Laik_Data* d)
{
    if (p->firstDataForPartitioning == d) {
        p->firstDataForPartitioning = d->nextPartitioningUser;
    }
    else {
        // search for previous item
        Laik_Data* dd = p->firstDataForPartitioning;
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
    if (p->firstDataForPartitioning == 0) {
        laik_removePartitioningFromGroup(p->group, p);
        laik_removePartitioningFromSpace(p->space, p);
        if (p->base)
            laik_removePartitioningFromBase(p->base, p);
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

// get number of mappings of this task
int laik_my_mapcount(Laik_Partitioning* p)
{
    if (!p->bordersValid)
        laik_calc_partitioning(p);

    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group
    assert(myid < p->group->size);
    int sCount = p->borders->off[myid+1] - p->borders->off[myid];
    if (sCount == 0) return 0;

    // map number of my last slice, incremented by one to get count
    return p->borders->tslice[p->borders->off[myid+1] - 1].mapNo + 1;
}


// get slice number <n> from the slices of this task
Laik_TaskSlice* laik_my_slice(Laik_Partitioning* p, int n)
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
    return (Laik_TaskSlice*) &(p->borders->tslice[o]);
}

Laik_TaskSlice* laik_my_slice_1d(Laik_Partitioning* p, int n,
                                 uint64_t* from, uint64_t* to)
{
    assert(p->space->dims == 1);
    Laik_TaskSlice* ts = laik_my_slice(p, n);
    if (ts == 0) {
        if (from) *from = 0;
        if (to) *to = 0;
        return 0;
    }

    switch(ts->type) {
    case TS_Generic: {
        Laik_TaskSlice_Gen* tsg = (Laik_TaskSlice_Gen*) ts;
        if (from) *from = tsg->s.from.i[0];
        if (to) *to = tsg->s.to.i[0];
        break;
    }
    case TS_Single1d: {
        Laik_TaskSlice_Single1d* tss = (Laik_TaskSlice_Single1d*) ts;
        if (from) *from = tss->idx;
        if (to) *to = tss->idx + 1;
        break;
    }
    default:
        assert(0);
    }

    return ts;
}

Laik_TaskSlice* laik_my_slice_2d(Laik_Partitioning* p, int n,
                                 uint64_t* x1, uint64_t* x2,
                                 uint64_t* y1, uint64_t* y2)
{
    assert(p->space->dims == 2);
    Laik_TaskSlice* ts = laik_my_slice(p, n);
    assert((ts == 0) || (ts->type == TS_Generic));
    Laik_TaskSlice_Gen* tsg = (Laik_TaskSlice_Gen*) ts;
    if (x1) *x1 = ts ? tsg->s.from.i[0] : 0;
    if (x2) *x2 = ts ? tsg->s.to.i[0] : 0;
    if (y1) *y1 = ts ? tsg->s.from.i[1] : 0;
    if (y2) *y2 = ts ? tsg->s.to.i[1] : 0;

    return ts;
}

Laik_TaskSlice* laik_my_slice_3d(Laik_Partitioning* p, int n,
                                 uint64_t* x1, uint64_t* x2,
                                 uint64_t* y1, uint64_t* y2,
                                 uint64_t* z1, uint64_t* z2)
{
    assert(p->space->dims == 3);
    Laik_TaskSlice* ts = laik_my_slice(p, n);
    assert((ts == 0) || (ts->type == TS_Generic));
    Laik_TaskSlice_Gen* tsg = (Laik_TaskSlice_Gen*) ts;
    if (x1) *x1 = ts ? tsg->s.from.i[0] : 0;
    if (x2) *x2 = ts ? tsg->s.to.i[0] : 0;
    if (y1) *y1 = ts ? tsg->s.from.i[1] : 0;
    if (y2) *y2 = ts ? tsg->s.to.i[1] : 0;
    if (z1) *z1 = ts ? tsg->s.from.i[2] : 0;
    if (z2) *z2 = ts ? tsg->s.to.i[2] : 0;

    return ts;
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


// give a partitioning a name, for debug output
void laik_set_partitioning_name(Laik_Partitioning* p, char* n)
{
    p->name = strdup(n);
}



// run a partitioner, returning newly calculated borders
// the partitioner may use other borders <otherBA> as reference
Laik_BorderArray* laik_run_partitioner(Laik_Partitioner* pr,
                                       Laik_Group* g, Laik_Space* space,
                                       Laik_BorderArray* otherBA)
{
    Laik_BorderArray* ba;

    ba = laik_allocBorders(g, space);
    if (otherBA) {
        assert(otherBA->group == g);
        // we do not check for same space, as there are use cases
        // where you want to derive a partitioning of one space from
        // the partitioning of another
    }
    (pr->run)(pr, ba, otherBA);
    updateBorderArrayOffsets(ba);

    // Check for mergable slices if requested
    if ((pr->flags & LAIK_PF_Merge) > 0)
        removeBorderArrayMergeSlices(ba);

    if (laik_log_begin(1)) {
        laik_log_append("run partitioner '%s' (group %d, myid %d, space '%s'):",
                        pr->name, g->gid, g->myid, space->name);
        laik_log_append("\n  other: ");
        laik_log_BorderArray(otherBA);
        laik_log_append("\n  new  : ");
        laik_log_BorderArray(ba);
        laik_log_flush(0);
    }

    if ((pr->flags & LAIK_PF_NoFullCoverage) == 0) {
        // by default, check if partitioning covers full space
        if (!coversSpace(ba))
            laik_log(LAIK_LL_Panic, "borders do not cover space");
    }

    return ba;
}


// set new partitioning borders
void laik_set_borders(Laik_Partitioning* p, Laik_BorderArray* ba)
{
    assert(p->group == ba->group);
    assert(p->space == ba->space);

    if (laik_log_begin(1)) {
        laik_log_append("setting borders for part '%s' (group %d, myid %d):\n  ",
                        p->name, ba->group->gid, ba->group->myid);
        laik_log_BorderArray(ba);
        laik_log_flush(0);
    }

    if (p->bordersValid && laik_border_isEqual(p->borders, ba)) {
        laik_log(1, "borders equal to original, nothing to do");
        return;
    }

    // visit all users of this partitioning:
    // first, all partitionings coupled to this as base
    Laik_Partitioning* pdep = p->firstPartitioningForBase;
    while(pdep) {
        assert(pdep->base == p);
        assert(pdep->partitioner);
        Laik_BorderArray* badep;
        badep = laik_run_partitioner(pdep->partitioner,
                                     pdep->group, pdep->space, ba);

        laik_set_borders(pdep, badep);
        pdep = pdep->nextPartitioningForBase;
    }
    // second, all data containers using this partitioning
    Laik_Data* d = p->firstDataForPartitioning;
    while(d) {
        laik_switchto_borders(d, ba);
        d = d->nextPartitioningUser;
    }

    if (p->borders)
        laik_freeBorderArray(p->borders);
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

    if (p->base) {
        assert(p->base->bordersValid);
    }

    assert(p->partitioner);
    ba = laik_run_partitioner(p->partitioner,
                              p->group, p->space,
                              p->base ? p->base->borders : 0);

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
            op->mapNo = toBA->tslice[o].mapNo;
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
                    op->fromMapNo = fromBA->tslice[o1].mapNo;
                    op->toSliceNo = o2 - toBA->off[myid];
                    op->toMapNo = toBA->tslice[o2].mapNo;

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
                op->slc = space->s; // complete space
                op->redOp = laik_get_reduction(fromFlow);
                op->rootTask = (root >= 0) ? root : -1;

                redCount++;
            }
        }

        // something to send?
        if (laik_do_copyout(fromFlow)) {
            for(int task = 0; task < count; task++) {
                if (task == myid) continue;
                for(int o1 = fromBA->off[myid]; o1 < fromBA->off[myid+1]; o1++) {

                    // everything the receiver has local, no need to send
                    // TODO: we only check for exact match to catch All
                    // FIXME: should print out a Warning/Error as the App
                    //        requests overwriting of values!
                    slc = &(fromBA->tslice[o1].s);
                    for(int o2 = fromBA->off[task]; o2 < fromBA->off[task+1]; o2++) {
                        if (laik_slice_isEqual(dims, slc,
                                               &(fromBA->tslice[o2].s))) {
                            slc = 0;
                            break;
                        }
                    }
                    if (slc == 0) continue;

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
                        op->mapNo = fromBA->tslice[o1].mapNo;
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
                for(int o1 = toBA->off[myid]; o1 < toBA->off[myid+1]; o1++) {

                    // everything we have local will not have been sent
                    // TODO: we only check for exact match to catch All
                    // FIXME: should print out a Warning/Error as the App
                    //        was requesting for overwriting of values!
                    slc = &(toBA->tslice[o1].s);
                    for(int o2 = fromBA->off[myid]; o2 < fromBA->off[myid+1]; o2++) {
                        if (laik_slice_isEqual(dims, slc,
                                               &(fromBA->tslice[o2].s))) {
                            slc = 0;
                            break;
                        }
                    }
                    if (slc == 0) continue;

                    for(int o2 = fromBA->off[task]; o2 < fromBA->off[task+1]; o2++) {

                        slc = laik_slice_intersect(dims,
                                                   &(fromBA->tslice[o2].s),
                                                   &(toBA->tslice[o1].s));
                        if (slc == 0) continue;

                        assert(recvCount < TRANSSLICES_MAX);
                        struct recvTOp* op = &(recv[recvCount]);
                        op->slc = *slc;
                        op->sliceNo = o1 - toBA->off[myid];
                        op->mapNo = toBA->tslice[o1].mapNo;
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

    Laik_Transition* t = malloc(tsize);
    t->dims = dims;
    t->actionCount = localCount + initCount + sendCount + recvCount + redCount;
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
        ba->off = malloc((newg->size +1) * sizeof(int));
    }
    ba->group = newg;
    updateBorderArrayOffsets(ba);
}

// migrate a partitioning defined on one task group to another group
// (no repartitioning: only works if partitions of removed tasks are empty)
bool laik_migrate_partitioning(Laik_Partitioning* p,
                               Laik_Group* newg)
{
    Laik_Group* oldg = p->group;
    assert(oldg != newg);

    if (p->bordersValid) {
        assert(p->borders && (p->borders->group == oldg));
        laik_migrate_borders(p->borders, newg);
    }

    laik_removePartitioningFromGroup(oldg, p);
    laik_addPartitioningForGroup(newg, p);
    p->group = newg;

    // make partitioning users (data containers) migrate to new group
    Laik_Data* d = p->firstDataForPartitioning;
    while(d) {
        assert(d->group == oldg);
        d->group = newg;
        d = d->nextPartitioningUser;
    }

    return true;
}

// migrate a partitioning defined on one task group to another group
// for the required repartitioning, either use the default partitioner
// or the given one. In the latter case, the partitioner is run
// on old group and is expected to produce no partitions for tasks
// to be removed
void laik_migrate_and_repartition(Laik_Partitioning* p, Laik_Group* newg,
                                  Laik_Partitioner* pr)
{
    if (!p) return;

    Laik_BorderArray* ba;
    if (pr) {
        ba = laik_run_partitioner(pr, p->group, p->space,
                                  p->bordersValid ? p->borders : 0);
    }
    else {
        ba = laik_run_partitioner(p->partitioner, newg, p->space, 0);
        laik_migrate_borders(ba, p->group);
    }
    laik_set_borders(p, ba);
    bool res = laik_migrate_partitioning(p, newg);
    assert(res);
}





/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017, 2018 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 or later.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "laik-internal.h"

// for string.h to declare strdup
#define __STDC_WANT_LIB_EXT2__ 1

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

// Space module initialization

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


// counter for space ID, for logging
static int space_id = 0;

// helpers

void laik_index_init(Laik_Index* i, int64_t i1, int64_t i2, int64_t i3)
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

//----------------------------------------------------------
// Ranges

void laik_range_init(Laik_Range* range, Laik_Space* space,
                     Laik_Index* from, Laik_Index* to)
{
    range->space = space;
    range->from = *from;
    range->to = *to;
}

void laik_range_init_copy(Laik_Range* dst, Laik_Range* src)
{
    *dst = *src;
}

void laik_range_init_1d(Laik_Range* range, Laik_Space* space,
                        int64_t from, int64_t to)
{
    assert(space && (space->dims == 1));
    range->space = space;
    range->from.i[0] = from;
    range->to.i[0] = to;
}

void laik_range_init_2d(Laik_Range* range, Laik_Space* space,
                        int64_t from1, int64_t to1,
                        int64_t from2, int64_t to2)
{
    assert(space && (space->dims == 2));
    range->space = space;
    range->from.i[0] = from1;
    range->from.i[1] = from2;
    range->to.i[0] = to1;
    range->to.i[1] = to2;
}

void laik_range_init_3d(Laik_Range* range, Laik_Space* space,
                        int64_t from1, int64_t to1,
                        int64_t from2, int64_t to2,
                        int64_t from3, int64_t to3)
{
    assert(space && (space->dims == 3));
    range->space = space;
    range->from.i[0] = from1;
    range->from.i[1] = from2;
    range->from.i[2] = from3;
    range->to.i[0] = to1;
    range->to.i[1] = to2;
    range->to.i[2] = to3;
}


bool laik_range_isEmpty(Laik_Range* range)
{
    // an invalid range (no space set) is considered empty
    if (range->space == 0) return true;

    int dims = range->space->dims;

    if (range->from.i[0] >= range->to.i[0])
        return true;

    if (dims>1) {
        if (range->from.i[1] >= range->to.i[1])
            return true;

        if (dims>2) {
            if (range->from.i[2] >= range->to.i[2])
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

// get the intersection of two ranges; return 0 if intersection is empty
Laik_Range* laik_range_intersect(const Laik_Range* r1, const Laik_Range* r2)
{
    static Laik_Range r;

    // intersection with invalid range gives invalid range
    if ((r1->space == 0) || (r2->space == 0)) {
        r.space = 0;
        return &r;
    }

    assert(r1->space == r2->space);
    int dims = r1->space->dims;
    r.space = r1->space;

    if (!intersectRange(r1->from.i[0], r1->to.i[0],
                        r2->from.i[0], r2->to.i[0],
                        &(r.from.i[0]), &(r.to.i[0])) ) return 0;
    if (dims>1) {
        if (!intersectRange(r1->from.i[1], r1->to.i[1],
                            r2->from.i[1], r2->to.i[1],
                            &(r.from.i[1]), &(r.to.i[1])) ) return 0;
        if (dims>2) {
            if (!intersectRange(r1->from.i[2], r1->to.i[2],
                                r2->from.i[2], r2->to.i[2],
                                &(r.from.i[2]), &(r.to.i[2])) ) return 0;
        }
    }
    return &r;
}

// expand range <dst> such that it contains <src>
void laik_range_expand(Laik_Range* dst, Laik_Range* src)
{
    // an invalid range stays invalid
    if (dst->space == 0) return;

    assert(src->space == dst->space);
    int dims = src->space->dims;

    if (src->from.i[0] < dst->from.i[0]) dst->from.i[0] = src->from.i[0];
    if (src->to.i[0] > dst->to.i[0]) dst->to.i[0] = src->to.i[0];
    if (dims == 1) return;

    if (src->from.i[1] < dst->from.i[1]) dst->from.i[1] = src->from.i[1];
    if (src->to.i[1] > dst->to.i[1]) dst->to.i[1] = src->to.i[1];
    if (dims == 2) return;

    if (src->from.i[2] < dst->from.i[2]) dst->from.i[2] = src->from.i[2];
    if (src->to.i[2] > dst->to.i[2]) dst->to.i[2] = src->to.i[2];
}

// is range <r1> contained in <r2>?
bool laik_range_within_range(const Laik_Range* r1, const Laik_Range* r2)
{
    // an invalid range never can be part of another
    if ((r1->space == 0) || (r2->space == 0)) return false;

    assert(r1->space == r2->space);

    int dims = r1->space->dims;
    if (r1->from.i[0] < r1->to.i[0]) {
        // not empty
        if (r1->from.i[0] < r2->from.i[0]) return false;
        if (r1->to.i[0] > r2->to.i[0]) return false;
    }
    if (dims == 1) return true;

    if (r1->from.i[1] < r1->to.i[1]) {
        // not empty
        if (r1->from.i[1] < r2->from.i[1]) return false;
        if (r1->to.i[1] > r2->to.i[1]) return false;
    }
    if (dims == 2) return true;

    if (r1->from.i[2] < r1->to.i[2]) {
        // not empty
        if (r1->from.i[2] < r2->from.i[2]) return false;
        if (r1->to.i[2] > r2->to.i[2]) return false;
    }
    return true;
}

// is range within space borders?
bool laik_range_within_space(const Laik_Range* range, const Laik_Space* sp)
{
    return laik_range_within_range(range, &(sp->range));
}

// are the ranges equal?
bool laik_range_isEqual(Laik_Range* r1, Laik_Range* r2)
{
    // an invalid range never can be part of another
    if ((r1->space == 0) || (r2->space == 0)) return false;
    if (r1->space != r2->space) return false;

    int dims = r1->space->dims;
    if (!laik_index_isEqual(dims, &(r1->from), &(r2->from))) return false;
    if (!laik_index_isEqual(dims, &(r1->to), &(r2->to))) return false;
    return true;
}


// number of indexes in the range
uint64_t laik_range_size(const Laik_Range* r)
{
    // invalid range?
    if (r->space == 0) return 0;

    int dims = r->space->dims;
    assert(r->to.i[0] >= r->from.i[0]);
    uint64_t size = (uint64_t) (r->to.i[0] - r->from.i[0]);
    if (dims > 1) {
        assert(r->to.i[1] >= r->from.i[1]);
        size *= (uint64_t) (r->to.i[1] - r->from.i[1]);
        if (dims > 2) {
            assert(r->to.i[2] >= r->from.i[2]);
            size *= (uint64_t) (r->to.i[2] - r->from.i[2]);
        }
    }
    return size;
}

// get the index range covered by the space
const Laik_Range* laik_space_asrange(Laik_Space* space)
{
    return &(space->range);
}

// number of indexes in the space
uint64_t laik_space_size(const Laik_Space* s)
{
    return laik_range_size(&(s->range));
}

// get the number of dimensions if this is a regular space
int laik_space_getdimensions(Laik_Space* space)
{
    return space->dims;
}


// is this a reduction?
bool laik_is_reduction(Laik_ReductionOperation redOp)
{
    return redOp != LAIK_RO_None;
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
    space->nextSpaceForInstance = 0;

    space->kvs = 0;

    // append this space to list of spaces used by LAIK instance
    laik_addSpaceForInstance(inst, space);

    return space;
}

// create a new index space object with an initial size
Laik_Space* laik_new_space_1d(Laik_Instance* i, int64_t s1)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 1;
    laik_range_init_1d(&(space->range), space, 0, s1);

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
    laik_range_init_2d(&(space->range), space, 0, s1, 0, s2);

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
    laik_range_init_3d(&(space->range), space, 0, s1, 0, s2, 0, s3);

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

// give a space a name, for debugging or referencing in space store
void laik_set_space_name(Laik_Space* s, char* n)
{
    // name only can be changed if not attached yet to space store yet
    assert(s->kvs == 0);
    s->name = strdup(n);
}

char* laik_space_serialize(Laik_Space* s, unsigned* psize)
{
    static char buf[100];

    int off = -1;
    if (s->dims == 1)
        off = sprintf(buf, "1(%" PRId64 "/%" PRId64 ")",
                      s->range.from.i[0], s->range.to.i[0]);
    else if (s->dims == 2)
        off = sprintf(buf, "2(%" PRId64 ",%" PRId64 "/%" PRId64 ",%" PRId64 ")",
                      s->range.from.i[0], s->range.from.i[1],
                      s->range.to.i[0], s->range.to.i[1]);
    else if (s->dims == 3)
        off = sprintf(buf, "3(%" PRId64 ",%" PRId64 ",%" PRId64
                      "/%" PRId64 ",%" PRId64 ",%" PRId64 ")",
                      s->range.from.i[0], s->range.from.i[1], s->range.from.i[2],
                      s->range.to.i[0], s->range.to.i[1], s->range.to.i[2]);
    assert(off > 0);
    if (psize) *psize = (unsigned) off;
    return buf;
}

bool laik_space_set(Laik_Space* s, char* v)
{
    if ((v[0] < '1') || (v[0] > '3')) return false;
    s->dims = v[0] - '0';
    if (s->dims == 1) {
        if (sscanf(v+1, "(%" SCNd64 "/%" SCNd64 ")",
                   &(s->range.from.i[0]), &(s->range.to.i[0]) ) != 2) return false;
    }
    else if (s->dims == 2) {
        if (sscanf(v+1, "(%" SCNd64 ",%" SCNd64 "/%" SCNd64 ",%" SCNd64 ")",
                   &(s->range.from.i[0]), &(s->range.from.i[1]),
                   &(s->range.to.i[0]), &(s->range.to.i[1]) ) != 4) return false;
    }
    else if (s->dims == 3) {
        if (sscanf(v+1, "(%" SCNd64 ",%" SCNd64 ",%" SCNd64
                   "/%" SCNd64 ",%" SCNd64 ",%" SCNd64 ")",
                   &(s->range.from.i[0]), &(s->range.from.i[1]), &(s->range.from.i[2]),
                   &(s->range.to.i[0]), &(s->range.to.i[1]), &(s->range.to.i[2]) ) != 6) return false;
    }
    return true;
}

static void update_space(Laik_KVStore* kvs, Laik_KVS_Entry* e)
{
    Laik_Space* s = (Laik_Space*) e->data;
    if (!s) {
        s = laik_new_space(kvs->inst);
        s->name = strdup(e->key);
        s->kvs = kvs;
        e->data = s;
    }
    bool res = laik_space_set(s, e->value);
    assert(res);
    laik_log(1, "space '%s' updated to '%s'", e->key, e->value);
}

Laik_KVStore* laik_spacestore(Laik_Instance* i)
{
    if (!i->spaceStore) {
        i->spaceStore = laik_kvs_new("space", i);
        laik_kvs_reg_callbacks(i->spaceStore,
                               update_space, update_space, 0);
    }

    return i->spaceStore;
}

// add a space to the space store of the instance
void laik_spacestore_set(Laik_Space* s)
{
    if (s->kvs == 0)
        s->kvs = laik_spacestore(s->inst);

    unsigned len;
    char* value = laik_space_serialize(s, &len);
    Laik_KVS_Entry* e = laik_kvs_set(s->kvs, s->name, len + 1, value);
    e->data = s;
}

Laik_Space* laik_spacestore_get(Laik_Instance* i, char* name)
{
    if (i->spaceStore == 0) return 0;

    Laik_KVS_Entry* e = laik_kvs_entry(i->spaceStore, name);
    if (!e) return 0;
    Laik_Space* s = (Laik_Space*) e->data;
    if (!s) {
        s = laik_new_space(i);
        s->name = strdup(name);
        s->kvs = i->spaceStore;
        bool res = laik_space_set(s, e->value);
        assert(res);
        e->data = s;
    }
    return s;
}

void laik_sync_spaces(Laik_Instance* i)
{
    Laik_KVStore* kvs = laik_spacestore(i);
    laik_kvs_sync(kvs);
}

// change the size of an index space, eventually triggering a repartitiong
void laik_change_space_1d(Laik_Space* s, int64_t from1, int64_t to1)
{
    assert(s->dims == 1);
    if ((s->range.from.i[0] == from1) && (s->range.to.i[0] == to1))
        return;

    s->range.from.i[0] = from1;
    s->range.to.i[0] = to1;

    // if range is in store, notify other processes on next sync
    if (s->kvs)
        laik_spacestore_set(s);

    // TODO: notify partitionings about space change
}

void laik_change_space_2d(Laik_Space* s,
                          int64_t from1, int64_t to1, int64_t from2, int64_t to2)
{
    assert(s->dims == 2);
    if ((s->range.from.i[0] == from1) && (s->range.to.i[0] == to1) &&
        (s->range.from.i[1] == from2) && (s->range.to.i[1] == to2)) return;

    s->range.from.i[0] = from1;
    s->range.to.i[0] = to1;
    s->range.from.i[1] = from2;
    s->range.to.i[1] = to2;

    // if range is in store, notify other processes on next sync
    if (s->kvs)
        laik_spacestore_set(s);

    // TODO: notify partitionings about space change
}

void laik_change_space_3d(Laik_Space* s, int64_t from1, int64_t to1,
                          int64_t from2, int64_t to2, int64_t from3, int64_t to3)
{
    assert(s->dims == 3);
    if ((s->range.from.i[0] == from1) && (s->range.to.i[0] == to1) &&
        (s->range.from.i[1] == from2) && (s->range.to.i[1] == to2) &&
        (s->range.from.i[2] == from3) && (s->range.to.i[2] == to3)) return;

    s->range.from.i[0] = from1;
    s->range.to.i[0] = to1;
    s->range.from.i[1] = from2;
    s->range.to.i[1] = to2;
    s->range.from.i[2] = from3;
    s->range.to.i[2] = to3;

    // if range is in store, notify other processes on next sync
    if (s->kvs)
        laik_spacestore_set(s);

    // TODO: notify partitionings about space change
}


//-----------------------
// Laik_TaskRange


// get range of a task range
const Laik_Range* laik_taskrange_get_range(Laik_TaskRange* trange)
{

    if (!trange) return 0;

    if (trange->list->trange)
        return &(trange->list->trange[trange->no].range);

    if (trange->list->tss1d) {
        static Laik_Range range;
        int64_t idx = trange->list->tss1d[trange->no].idx;
        laik_range_init_1d(&range, trange->list->space, idx, idx + 1);
        return &range;
    }
    return 0;
}

// get the process rank of an task range
int laik_taskrange_get_task(Laik_TaskRange* trange)
{
    assert(trange && trange->list);
    if (trange->list->trange)
        return trange->list->trange[trange->no].task;
    if (trange->list->tss1d)
        return trange->list->tss1d[trange->no].task;

    return -1;
}



int laik_taskrange_get_mapNo(Laik_TaskRange* trange)
{
    assert(trange && trange->list);
    // does the partitioning store ranges as single indexes? Only one map!
    if (trange->list->tss1d) return 0;

    Laik_TaskRange_Gen* tsg = &(trange->list->trange[trange->no]);
    return tsg->mapNo;
}

int laik_taskrange_get_tag(Laik_TaskRange* trange)
{
    assert(trange && trange->list);
    // does the partitioning store ranges as single indexes? Always tag 1
    if (trange->list->tss1d) return 1;

    Laik_TaskRange_Gen* tsg = &(trange->list->trange[trange->no]);
    return tsg->tag;
}


// applications can attach arbitrary values to a TaskRange, to be
// passed from application-specific partitioners to range processing
void* laik_taskrange_get_data(Laik_TaskRange* trange)
{
    assert(trange && trange->list);
    // does the partitioning store ranges as single indexes? No data!
    if (trange->list->tss1d) return 0;

    Laik_TaskRange_Gen* tsg = &(trange->list->trange[trange->no]);
    return tsg->data;
}

void laik_taskrange_set_data(Laik_TaskRange* trange, void* data)
{
    assert(trange && trange->list);
    // does the partitioning store ranges as single indexes? No data to set!
    assert(trange->list->tss1d == 0);

    Laik_TaskRange_Gen* tsg = &(trange->list->trange[trange->no]);
    tsg->data = data;
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



//-------------------------------------------------------------------------
// Laik_Transition
//


// helper functions for laik_calc_transition

// TODO:
// - quadratic complexity for 2d/3d spaces
// - for 1d, does not cope with overlapping ranges belonging to same task

// print verbose debug output for creating ranges for reductions?
#define DEBUG_REDUCTIONRANGES 1


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
typedef struct _RangeBorder {
    int64_t b;
    int task;
    int rangeNo, mapNo;
    unsigned int isStart :1;
    unsigned int isInput :1;
} RangeBorder;

static RangeBorder* borderList = 0;
int borderListSize = 0, borderListCount = 0;

static
void cleanBorderList()
{
    borderListCount = 0;
}

static
void freeBorderList()
{
    free(borderList);
    borderList = 0;
    borderListCount = 0;
    borderListSize = 0;
}

static
void appendBorder(int64_t b, int task, int rangeNo, int mapNo,
                  bool isStart, bool isInput)
{
    if (borderListCount == borderListSize) {
        // enlarge list
        borderListSize = (borderListSize + 10) * 2;
        borderList = realloc(borderList, borderListSize * sizeof(RangeBorder));
        if (!borderList) {
            laik_panic("Out of memory allocating memory for Laik_Transition");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    RangeBorder *sb = &(borderList[borderListCount]);
    borderListCount++;

    sb->b = b;
    sb->task = task;
    sb->rangeNo = rangeNo;
    sb->mapNo = mapNo;
    sb->isStart = isStart ? 1 : 0;
    sb->isInput = isInput ? 1 : 0;

#ifdef DEBUG_REDUCTIONRANGES
    laik_log(1, "  add border %lld, task %d range/map %d/%d (%s, %s)",
             (long long int) b, task, rangeNo, mapNo,
             isStart ? "start" : "end", isInput ? "input" : "output");
#endif
}

static int rb_cmp(const void *p1, const void *p2)
{
    const RangeBorder* sb1 = (const RangeBorder*) p1;
    const RangeBorder* sb2 = (const RangeBorder*) p2;
    // order by border, at same point first close range
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
struct localTOp* appendLocalTOp(Laik_Range* range,
                                int fromRangeNo, int toRangeNo,
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

    op->range = *range;
    op->fromRangeNo = fromRangeNo;
    op->toRangeNo = toRangeNo;
    op->fromMapNo = fromMapNo;
    op->toMapNo = toMapNo;

    return op;
}

static
struct initTOp* appendInitTOp(Laik_Range* range,
                              int rangeNo, int mapNo,
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

    op->range = *range;
    op->rangeNo = rangeNo;
    op->mapNo = mapNo;
    op->redOp = redOp;

    return op;
}

static
struct sendTOp* appendSendTOp(Laik_Range* range,
                              int rangeNo, int mapNo, int toTask)
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

    op->range = *range;
    op->rangeNo = rangeNo;
    op->mapNo = mapNo;
    op->toTask = toTask;

    return op;
}

static
struct recvTOp* appendRecvTOp(Laik_Range* range,
                              int rangeNo, int mapNo, int fromTask)
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

    op->range = *range;
    op->rangeNo = rangeNo;
    op->mapNo = mapNo;
    op->fromTask = fromTask;

    return op;
}

static
struct redTOp* appendRedTOp(Laik_Range* range,
                            Laik_ReductionOperation redOp,
                            int inputGroup, int outputGroup,
                            int myInputRangeNo, int myOutputRangeNo,
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

    op->range = *range;
    op->redOp = redOp;
    op->inputGroup = inputGroup;
    op->outputGroup = outputGroup;
    op->myInputRangeNo = myInputRangeNo;
    op->myOutputRangeNo = myOutputRangeNo;
    op->myInputMapNo = myInputMapNo;
    op->myOutputMapNo = myOutputMapNo;


    return op;
}

static
bool oneInputIsCopy(Laik_ReductionOperation redOp)
{
    // currently all reduction types available in Laik simply copy
    // the value to all outputs if there is only one input.
    // however, this cannot be assumed for user-provided reductions
    // TODO: allow to specify whether custom reductions have this property
    return (int) redOp < LAIK_RO_Custom;
}

// find all ranges where this task takes part in a reduction, and add
// them to the reduction operation list.
// TODO: we only support one mapping in each task for reductions
//       better: support multiple mappings for same index in same task
static
void calcAddReductions(int tflags,
                       Laik_Group* group,
                       Laik_ReductionOperation redOp,
                       Laik_Partitioning* fromP, Laik_Partitioning* toP)
{
    // TODO: only for 1d
    assert(fromP->space->dims == 1);
    assert(fromP->space == toP->space);

    Laik_RangeList *fromRL, *toRL;
    // need at least intersection of own ranges in fromP/toP to calculate transition
    fromRL = laik_partitioning_interranges(fromP, toP);
    toRL = laik_partitioning_interranges(toP, fromP);
    if ((fromRL == 0) || (toRL == 0)) {
        // required ranges for transition calculation not calculated yet
        laik_panic("Transition calculation not possible without pre-calculated ranges");
        exit(1); // not actually needed, laik_panic never returns
    }

    if (laik_log_begin(1)) {
        laik_log_append("calc '");
        laik_log_Reduction(redOp);
        laik_log_flush("' ops for '%s' (%d ranges) => '%s' (%d ranges)",
                       fromP->name, fromRL->count, toP->name, toRL->count);
    }

    // add range borders of all tasks
    cleanBorderList();
    int rangeNo, lastTask, lastMapNo;
    rangeNo = 0;
    lastTask = -1;
    lastMapNo = -1;
    for(unsigned int i = 0; i < fromRL->count; i++) {
        Laik_TaskRange_Gen* ts = &(fromRL->trange[i]);
        // reset rangeNo to 0 on every task/mapNo change
        if ((ts->task != lastTask) || (ts->mapNo != lastMapNo)) {
            rangeNo = 0;
            lastTask = ts->task;
            lastMapNo = ts->mapNo;
        }
        appendBorder(ts->range.from.i[0], ts->task, rangeNo, ts->mapNo, true, true);
        appendBorder(ts->range.to.i[0], ts->task, rangeNo, ts->mapNo, false, true);
        rangeNo++;
    }
    lastTask = -1;
    lastMapNo = -1;
    for(unsigned int i = 0; i < toRL->count; i++) {
        Laik_TaskRange_Gen* tr = &(toRL->trange[i]);
        // reset rangeNo to 0 on every task/mapNo change
        if ((tr->task != lastTask) || (tr->mapNo != lastMapNo)) {
            rangeNo = 0;
            lastTask = tr->task;
            lastMapNo = tr->mapNo;
        }
        appendBorder(tr->range.from.i[0], tr->task, rangeNo, tr->mapNo, true, false);
        appendBorder(tr->range.to.i[0], tr->task, rangeNo, tr->mapNo, false, false);
        rangeNo++;
    }

    if (borderListCount == 0) return;

    // order by border to travers in border order
    qsort(borderList, borderListCount, sizeof(RangeBorder), rb_cmp);

#define MAX_TASKS 65536
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
    int myInputRangeNo = -1, myOutputRangeNo = -1;
    int myInputMapNo = -1, myOutputMapNo = -1;

    Laik_Range range;
    // all ranges are from same space
    range.space = fromP->space;

    for(int i = 0; i < borderListCount; i++) {
        RangeBorder* sb = &(borderList[i]);

#ifdef DEBUG_REDUCTIONRANGES
        laik_log(1, "at border %lld, task %d (range %d, map %d): %s for %s",
                 (long long int) sb->b, sb->task, sb->rangeNo, sb->mapNo,
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
                    myInputRangeNo = sb->rangeNo;
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
                    myOutputRangeNo = sb->rangeNo;
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

#ifdef DEBUG_REDUCTIONRANGES
            char* act[] = {"(none)", "input", "output", "in & out"};
            laik_log(1, "  range (%lld - %lld), my activity: %s",
                     (long long int) sb->b,
                     (long long int) nextBorder, act[myActivity]);
#endif

            if (myActivity > 0) {
                assert(isInTaskGroup(&inputGroup, myid) ||
                       isInTaskGroup(&outputGroup, myid));

                range.from.i[0] = sb->b;
                range.to.i[0] = nextBorder;

                // check for special case: one input, ie. no reduction needed
                if (inputGroup.count == 1) {
                    // should not check for special cases if not true
                    assert(oneInputIsCopy(redOp));

                    if (inputGroup.task[0] == myid) {
                        // only this task as input

                        if ((outputGroup.count == 1) &&
                            (outputGroup.task[0] == myid)) {

                            // local (copy) operation
                            appendLocalTOp(&range,
                                           myInputRangeNo, myOutputRangeNo,
                                           myInputMapNo, myOutputMapNo);
#ifdef DEBUG_REDUCTIONRANGES
                            laik_log(1, "  adding local (special reduction)"
                                        " (%lld - %lld) from %d/%d to %d/%d (range/map)",
                                     (long long int) range.from.i[0],
                                     (long long int) range.to.i[0],
                                     myInputRangeNo, myInputMapNo,
                                     myOutputRangeNo, myOutputMapNo);
#endif
                            continue;
                        }

                        if (!(tflags & LAIK_TF_KEEP_REDUCTIONS)) {
                            // TODO: broadcasts might be supported in backend
                            for(int out = 0; out < outputGroup.count; out++) {
                                if (outputGroup.task[out] == myid) {
                                    // local (copy) operation
                                    appendLocalTOp(&range,
                                                   myInputRangeNo, myOutputRangeNo,
                                                   myInputMapNo, myOutputMapNo);
#ifdef DEBUG_REDUCTIONRANGES
                                    laik_log(1, "  adding local (special reduction)"
                                                " (%lld - %lld) from %d/%d to %d/%d (range/map)",
                                             (long long int) range.from.i[0],
                                            (long long int) range.to.i[0],
                                            myInputRangeNo, myInputMapNo,
                                            myOutputRangeNo, myOutputMapNo);
#endif
                                    continue;
                                }

                                // send operation
                                appendSendTOp(&range,
                                              myInputRangeNo, myInputMapNo,
                                              outputGroup.task[out]);
#ifdef DEBUG_REDUCTIONRANGES
                                laik_log(1, "  adding send (special reduction)"
                                            " (%lld - %lld) range/map %d/%d to T%d",
                                         (long long int) range.from.i[0],
                                        (long long int) range.to.i[0],
                                        myInputRangeNo, myInputMapNo,
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
                                appendRecvTOp(&range,
                                              myOutputRangeNo, myOutputMapNo,
                                              inputGroup.task[0]);

#ifdef DEBUG_REDUCTIONRANGES
                                laik_log(1, "  adding recv (special reduction)"
                                            " (%lld - %lld) range/map %d/%d from T%d",
                                         (long long int) range.from.i[0],
                                        (long long int) range.to.i[0],
                                        myOutputRangeNo, myOutputMapNo,
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

#ifdef DEBUG_REDUCTIONRANGES
                laik_log_begin(1);
                laik_log_append("  adding reduction (%lu - %lu), in %d:(",
                                range.from.i[0], range.to.i[0], in);
                for(int i = 0; i < groupList[in].count; i++) {
                    if (i > 0) laik_log_append(",");
                    laik_log_append("T%d", groupList[in].task[i]);
                }
                laik_log_append("), out %d:(", out);
                for(int i = 0; i < groupList[out].count; i++) {
                    if (i > 0) laik_log_append(",");
                    laik_log_append("T%d", groupList[out].task[i]);
                }
                laik_log_flush("), in %d/%d out %d/%d (range/map)",
                               myInputRangeNo, myInputMapNo,
                               myOutputRangeNo, myOutputMapNo);
#endif

                // convert to all-group if possible
                if (groupList[in].count == group->size) in = -1;
                if (groupList[out].count == group->size) out = -1;

                assert(redOp != LAIK_RO_None); // must be a real reduction
                appendRedTOp(&range, redOp, in, out,
                             myInputRangeNo, myOutputRangeNo,
                             myInputMapNo, myOutputMapNo);
            }
        }
    }
    // all tasks should be removed from input/output groups
    assert(inputGroup.count == 0);
    assert(outputGroup.count == 0);
    freeBorderList();
}

static int trans_id = 0;

// Calculate communication required for transitioning between partitionings
Laik_Transition*
do_calc_transition(Laik_Space* space,
                   Laik_Partitioning* fromP, Laik_Partitioning* toP,
                   Laik_DataFlow flow, Laik_ReductionOperation redOp)
{
    Laik_Range* range;

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
        assert(flow == LAIK_DF_None);
        assert(fromP->space == space);

        group = fromP->group;
    }
    else {
        // to and from set
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
    unsigned int o, o1, o2;

    // request to initialize values?
    if ((toP != 0) && (flow == LAIK_DF_Init)) {

        // own ranges enough
        Laik_RangeList* toRL = laik_partitioning_myranges(toP);
        if (!toRL) {
            laik_panic("Own ranges not known for init in transition calculation");
            exit(1); // not actually needed, laik_panic never returns
        }

        for(o = toRL->off[myid]; o < toRL->off[myid+1]; o++) {
            if (laik_range_isEmpty(&(toRL->trange[o].range))) continue;

            assert(redOp != LAIK_RO_None);
            appendInitTOp( &(toRL->trange[o].range),
                           o - toRL->off[myid],
                           toRL->trange[o].mapNo,
                           redOp);
        }
    }

    if ((fromP != 0) && (toP != 0) && (flow == LAIK_DF_Preserve)) {

        // check for 1d with preserving data between partitionings
        if (dims == 1) {

            // just check for reduction action
            // TODO: Do this always, remove other cases
            calcAddReductions(tflags, group, redOp, fromP, toP);
        }
        else {
            // we need intersection of own ranges in fromP/toP
            Laik_RangeList* fromRL = laik_partitioning_allranges(fromP);
            Laik_RangeList* toRL = laik_partitioning_allranges(toP);
            if ((fromRL == 0) || (toRL == 0)) {
                laik_panic("Ranges not known for transition calculation");
                exit(1); // not actually needed, laik_panic never returns
            }

            // determine local ranges to keep
            // (may need local copy if from/to mappings are different).
            // reductions are not handled here, but by backend
            for(o1 = fromRL->off[myid]; o1 < fromRL->off[myid+1]; o1++) {
                for(o2 = toRL->off[myid]; o2 < toRL->off[myid+1]; o2++) {
                    range = laik_range_intersect(&(fromRL->trange[o1].range),
                                               &(toRL->trange[o2].range));
                    if (range == 0) continue;

                    appendLocalTOp(range,
                                   o1 - fromRL->off[myid],
                                   o2 - toRL->off[myid],
                                   fromRL->trange[o1].mapNo,
                                   toRL->trange[o2].mapNo);
                }
            }

            // something to reduce?
            if (laik_is_reduction(redOp)) {
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
                    // complete space, always rangeNo 0 and mapNo 0
                    appendRedTOp( &(space->range), redOp,
                                  -1, outputGroup, 0, 0, 0, 0);
                }
                else {
                    assert(dims == 1);
                    calcAddReductions(tflags, group, redOp, fromP, toP);
                }
            }
            else { // no reduction

                // something to receive not coming from a reduction?
                for(int task = 0; task < taskCount; task++) {
                    if (task == myid) continue;
                    for(o1 = toRL->off[myid]; o1 < toRL->off[myid+1]; o1++) {

                        // everything we have local will not have been sent
                        // TODO: we only check for exact match to catch All
                        // FIXME: should print out a Warning/Error as the App
                        //        was requesting for overwriting of values!
                        range = &(toRL->trange[o1].range);
                        for(o2 = fromRL->off[myid]; o2 < fromRL->off[myid+1]; o2++) {
                            if (laik_range_isEqual(range,
                                                   &(fromRL->trange[o2].range))) {
                                range = 0;
                                break;
                            }
                        }
                        if (range == 0) continue;

                        for(o2 = fromRL->off[task]; o2 < fromRL->off[task+1]; o2++) {

                            range = laik_range_intersect(&(fromRL->trange[o2].range),
                                                       &(toRL->trange[o1].range));
                            if (range == 0) continue;

                            appendRecvTOp(range, o1 - toRL->off[myid],
                                          toRL->trange[o1].mapNo, task);
                        }
                    }
                }
            }

            // something to send?
            for(int task = 0; task < taskCount; task++) {
                if (task == myid) continue;
                for(o1 = fromRL->off[myid]; o1 < fromRL->off[myid+1]; o1++) {

                    // everything the receiver has local, no need to send
                    // TODO: we only check for exact match to catch All
                    // FIXME: should print out a Warning/Error as the App
                    //        requests overwriting of values!
                    range = &(fromRL->trange[o1].range);
                    for(o2 = fromRL->off[task]; o2 < fromRL->off[task+1]; o2++) {
                        if (laik_range_isEqual(range,
                                               &(fromRL->trange[o2].range))) {
                            range = 0;
                            break;
                        }
                    }
                    if (range == 0) continue;

                    // we may send multiple messages to same task
                    for(o2 = toRL->off[task]; o2 < toRL->off[task+1]; o2++) {

                        range = laik_range_intersect(&(fromRL->trange[o1].range),
                                                   &(toRL->trange[o2].range));
                        if (range == 0) continue;

                        appendSendTOp(range, o1 - fromRL->off[myid],
                                      fromRL->trange[o1].mapNo, task);
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

    t->id = trans_id++;
    t->name = strdup("trans-0     ");
    sprintf(t->name, "trans-%d", t->id);

    t->flags = tflags;
    t->space = space;
    t->group = group;
    t->fromPartitioning = fromP;
    t->toPartitioning = toP;
    t->flow = flow;
    t->redOp = redOp;

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
        laik_log_append("calculated transition ");
        laik_log_Transition(t, true);
        laik_log_flush(0);
    }

    return t;
}


// Calculate communication required for transitioning between partitionings
Laik_Transition*
laik_calc_transition(Laik_Space* space,
                     Laik_Partitioning* fromP, Laik_Partitioning* toP,
                     Laik_DataFlow flow, Laik_ReductionOperation redOp)
{
    if (fromP && toP) {
        // a transition always needs to be between the same process group
        assert(fromP->group == toP->group);
    }

    Laik_Transition* t;
    t = do_calc_transition(space, fromP, toP, flow, redOp);

    if (laik_log_begin(2)) {
        if (!t)
            laik_log_flush("calc transition: invalid");
        else {
            laik_log_append("calc transition '%s' (", t->name);
            laik_log_Transition(t, false);
            laik_log_flush("): %d init, %d loc, %d red, %d send, %d recv",
                       t->initCount, t->localCount, t->redCount,
                       t->sendCount, t->recvCount);
        }
    }

    return t;
}

void laik_free_transition(Laik_Transition* t)
{
    if (!t) return;

    laik_log(1, "free transition '%s'", t->name);
    free(t);
}

// return size of task group with ID <subgroup> in transition <t>
int laik_trans_groupCount(Laik_Transition* t, int subgroup)
{
    if (subgroup == -1)
        return t->group->size;

    assert((subgroup >= 0) && (subgroup < t->subgroupCount));
    return t->subgroup[subgroup].count;
}

// return task of <i>'th task in group with ID <subgroup> in transition <t>
int laik_trans_taskInGroup(Laik_Transition* t, int subgroup, int i)
{
    if (subgroup == -1) {
        assert((i >= 0) && (i < t->group->size));
        return i;
    }

    assert((subgroup >= 0) && (subgroup < t->subgroupCount));
    assert((i >= 0) && (i < t->subgroup[subgroup].count));
    return t->subgroup[subgroup].task[i];
}

// true if a task is part of the group with ID <subgroup> in transition <t>
bool laik_trans_isInGroup(Laik_Transition* t, int subgroup, int task)
{
    // all-group?
    if (subgroup == -1) return true;

    assert(subgroup < t->subgroupCount);
    TaskGroup* tg = &(t->subgroup[subgroup]);
    for(int i = 0; i < tg->count; i++)
        if (tg->task[i] == task) return true;
    return false;
}



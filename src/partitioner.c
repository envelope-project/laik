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

#include <assert.h>
#include <stdint.h>


//
// Laik_Partitioner
//

Laik_Partitioner* laik_new_partitioner(const char* name,
                                       laik_run_partitioner_t run,
                                       void* d,
                                       Laik_PartitionerFlag flags)
{
    Laik_Partitioner* pr;
    pr = malloc(sizeof(Laik_Partitioner));
    if (!pr) {
        laik_panic("Out of memory allocating Laik_Partitioner object");
        exit(1); // not actually needed, laik_panic never returns
    }

    pr->name = name;
    pr->run = run;
    pr->flags = flags;
    pr->data = d;

    return pr;
}

// public: get a custom data pointer from the partitioner
void* laik_partitioner_data(Laik_Partitioner* partitioner)
{
    return partitioner->data;
}


// running a partitioner

Laik_RangeList* laik_run_partitioner(Laik_PartitionerParams* params,
                                      Laik_RangeFilter* filter)
{
    if (params->other) {
        assert(params->other->group == params->group);
        // we do not check for same space, as there are use cases
        // where you want to derive a partitioning of one space from
        // the partitioning of another
    }

    Laik_RangeList* array;
    array = laik_rangelist_new(params->space, params->group->size);

    Laik_RangeReceiver r;
    r.params = params;
    r.list = array;
    r.filter = filter;

    Laik_Partitioner* pr = params->partitioner;

    (pr->run)(&r, params);

    bool doMerge = (pr->flags & LAIK_PF_Merge) > 0;
    laik_rangelist_freeze(array, doMerge);

    if (laik_log_begin(1)) {
        laik_log_append("run partitioner '%s' (group %d, space '%s'):",
                        pr->name, params->group->gid, params->space->name);
        if (params->other) {
            laik_log_append("\n  other: ");
            laik_log_Partitioning(params->other);
        }
        laik_log_append("\n  ");
        laik_log_RangeList(array);
        laik_log_flush(0);
    }

    // by default, check if partitioning covers full space
    // unless partitioner has flag to not do it, or task filter is used
    bool doCoverageCheck = false;
    if (pr) doCoverageCheck = (pr->flags & LAIK_PF_NoFullCoverage) == 0;
    if (filter) doCoverageCheck = false;
    if (doCoverageCheck) {
        if (!laik_rangelist_coversSpace(array))
            laik_log(LAIK_LL_Panic, "range list does not cover space");
    }

    return array;
}

// partitioner API: add a range
// - specify range groups by giving ranges the same tag
// - arbitrary data can be attached to ranges if no merge step is done
void laik_append_range(Laik_RangeReceiver* r,
                       int task, const Laik_Range *s, int tag, void* data)
{
    // if filter is installed, check if we should store the range
    Laik_RangeFilter* sf = r->filter;
    if (sf) {
        bool res = (sf->filter_func)(sf, task, s);
        laik_log(1,"appending range %d:[%lld;%lld[: %s",
                 task,
                 (long long) s->from.i[0], (long long) s->to.i[0],
                 res ? "keep":"skip");
        if (res == false) return;
    }

    laik_rangelist_append(r->list, task, s, tag, data);
}

// partitioner API: add a single 1d index range, optimized for fast merging
// if a partitioner only uses this method, an optimized internal format is used
void laik_append_index_1d(Laik_RangeReceiver* r, int task, int64_t idx)
{
    Laik_RangeFilter* sf = r->filter;
    if (sf) {
        Laik_Range range;
        laik_range_init_1d(&range, r->list->space, idx, idx + 1);
        bool res = (sf->filter_func)(sf, task, &range);
        laik_log(1,"appending range %d:[%lld;%lld[: %s",
                 task,
                 (long long) range.from.i[0], (long long) range.to.i[0],
                 res ? "keep":"skip");
        if (res == false) return;
    }

    laik_rangelist_append_single1d(r->list, task, idx);
}




// Simple partitioners

// all-partitioner: all tasks have access to all indexes

void runAllPartitioner(Laik_RangeReceiver* r, Laik_PartitionerParams* p)
{
    Laik_Space* space = p->space;
    Laik_Group* g = p->group;

    for(int task = 0; task < g->size; task++) {
        laik_append_range(r, task, &(space->range), 0, 0);
    }
}

Laik_Partitioner* laik_new_all_partitioner()
{
    return laik_new_partitioner("all", runAllPartitioner, 0, 0);
}

// master-partitioner: only task 0 has access to all indexes

void runMasterPartitioner(Laik_RangeReceiver* r, Laik_PartitionerParams* p)
{
    // only full range for master
    laik_append_range(r, 0, &(p->space->range), 0, 0);
}

Laik_Partitioner* laik_new_master_partitioner()
{
    return laik_new_partitioner("master", runMasterPartitioner, 0, 0);
}

// copy-partitioner: copy the borders from base partitioning
//
// we assume 1d partitioning on spaces with multiple dimensions.
// Parameters are the partitioned dimension to copy borders from
// and the dimension to copy partitioning to

typedef struct _Laik_CopyPartitionerData Laik_CopyPartitionerData;
struct _Laik_CopyPartitionerData {
    int fromDim, toDim; // only supports 1d partitionings
};

void runCopyPartitioner(Laik_RangeReceiver* r, Laik_PartitionerParams* p)
{
    Laik_CopyPartitionerData* data;
    data = (Laik_CopyPartitionerData*) p->partitioner->data;
    assert(data);
    int fromDim = data->fromDim;
    int toDim = data->toDim;

    Laik_Partitioning* other = p->other;
    assert(other->group == p->group); // must use same task group
    assert((fromDim >= 0) && (fromDim < other->space->dims));
    assert((toDim >= 0) && (toDim < p->space->dims));

    int count = laik_partitioning_rangecount(other);
    for(int i = 0; i < count; i++) {
        Laik_TaskRange* ts = laik_partitioning_get_taskrange(other, i);
        const Laik_Range* s = laik_taskrange_get_range(ts);
        Laik_Range range = p->space->range;
        range.from.i[toDim] = s->from.i[fromDim];
        range.to.i[toDim] = s->to.i[fromDim];
        laik_append_range(r, laik_taskrange_get_task(ts), &range,
                          laik_taskrange_get_tag(ts), 0);
    }
}

Laik_Partitioner* laik_new_copy_partitioner(int fromDim, int toDim)
{
    Laik_CopyPartitionerData* data;
    data = malloc(sizeof(Laik_CopyPartitionerData));
    if (!data) {
        laik_panic("Out of memory allocating Laik_CopyPartitionerData object");
        exit(1); // not actually needed, laik_panic never returns
    }

    data->fromDim = fromDim;
    data->toDim = toDim;

    return laik_new_partitioner("copy", runCopyPartitioner, data, 0);
}


// corner-halo partitioner: extend borders of other partitioning
//  including corners - e.g. for 9-point 2d stencil
void runCornerHaloPartitioner(Laik_RangeReceiver* r, Laik_PartitionerParams* p)
{
    Laik_Partitioning* other = p->other;
    assert(other->group == p->group); // must use same task group
    assert(other->space == p->space);

    int dims = p->space->dims;
    int d = *((int*) p->partitioner->data);

    // take all ranges and extend them if possible
    int count = laik_partitioning_rangecount(other);
    for(int i = 0; i < count; i++) {
        Laik_TaskRange* ts = laik_partitioning_get_taskrange(other, i);
        const Laik_Range* s = laik_taskrange_get_range(ts);
        const Laik_Index* from = &(s->from);
        const Laik_Index* to = &(s->to);
        Laik_Range range = p->space->range;

        if (from->i[0] > range.from.i[0] + d)
            range.from.i[0] = from->i[0] - d;
        if (to->i[0] < range.to.i[0] - d)
            range.to.i[0] = to->i[0] + d;

        if (dims > 1) {
            if (from->i[1] > range.from.i[1] + d)
                range.from.i[1] = from->i[1] - d;
            if (to->i[1] < range.to.i[1] - d)
                range.to.i[1] = to->i[1] + d;

            if (dims > 2) {
                if (from->i[2] > range.from.i[2] + d)
                    range.from.i[2] = from->i[2] - d;
                if (to->i[2] < range.to.i[2] - d)
                    range.to.i[2] = to->i[2] + d;
            }
        }
        laik_append_range(r, laik_taskrange_get_task(ts), &range,
                          laik_taskrange_get_tag(ts), 0);
    }
}

Laik_Partitioner* laik_new_cornerhalo_partitioner(int depth)
{
    int* data = malloc(sizeof(int));
    assert(data);
    *data = depth;

    return laik_new_partitioner("cornerhalo",
                                runCornerHaloPartitioner, data, 0);
}


// halo partitioner: extend borders of another partitioning
// excluding corners, e.g. for 5-point 2d stencil.
// this creates multiple ranges for each original range, and uses tags
// to mark the halos of a range to go into same mapping. For this, the
// tags of original ranges is used, which are expected >0 to allow for
// specification of range groups (tag 0 is special, produces a group per range)
void runHaloPartitioner(Laik_RangeReceiver* r, Laik_PartitionerParams* p)
{
    Laik_Partitioning* other = p->other;
    assert(other->group == p->group); // must use same task group
    assert(other->space == p->space);

    int dims = p->space->dims;
    int depth = *((int*) p->partitioner->data);
    Laik_Range sp = p->space->range;

    // take all ranges and extend them if possible
    int count = laik_partitioning_rangecount(other);
    for(int i = 0; i < count; i++) {
        Laik_TaskRange* ts = laik_partitioning_get_taskrange(other, i);
        const Laik_Range* s = laik_taskrange_get_range(ts);
        int task = laik_taskrange_get_task(ts);
        int tag = laik_taskrange_get_tag(ts);
        assert(tag > 0); // tag must be >0 to specify range groups

        Laik_Range range = *s;
        laik_append_range(r, task, &range, tag, 0);

        if (range.from.i[0] > sp.from.i[0] + depth) {
            range.to.i[0] = range.from.i[0];
            range.from.i[0] -= depth;
            laik_append_range(r, task, &range, tag, 0);
        }
        range = *s;
        if (range.to.i[0] < sp.to.i[0] - depth) {
            range.from.i[0] = range.to.i[0];
            range.to.i[0] += depth;
            laik_append_range(r, task, &range, tag, 0);
        }

        if (dims > 1) {
            range = *s;
            if (range.from.i[1] > sp.from.i[1] + depth) {
                range.to.i[1] = range.from.i[1];
                range.from.i[1] -= depth;
                laik_append_range(r, task, &range, tag, 0);
            }
            range = *s;
            if (range.to.i[1] < sp.to.i[1] - depth) {
                range.from.i[1] = range.to.i[1];
                range.to.i[1] += depth;
                laik_append_range(r, task, &range, tag, 0);
            }

            if (dims > 2) {
                range = *s;
                if (range.from.i[2] > sp.from.i[2] + depth) {
                    range.to.i[2] = range.from.i[2];
                    range.from.i[2] -= depth;
                    laik_append_range(r, task, &range, tag, 0);
                }
                range = *s;
                if (range.to.i[2] < sp.to.i[2] - depth) {
                    range.from.i[2] = range.to.i[2];
                    range.to.i[2] += depth;
                    laik_append_range(r, task, &range, tag, 0);
                }
            }
        }
    }
}

Laik_Partitioner* laik_new_halo_partitioner(int depth)
{
    int* data = malloc(sizeof(int));
    assert(data);
    *data = depth;

    return laik_new_partitioner("halo", runHaloPartitioner, data, 0);
}


// bisection partitioner

// recursive helper: distribute range <s> to tasks in range [fromTask;toTask[
static void doBisection(Laik_RangeReceiver* r, Laik_PartitionerParams* p,
                        Laik_Range* s, int fromTask, int toTask)
{
    int tag = 1; // TODO: make it a parameter

    assert(toTask > fromTask);
    if (toTask - fromTask == 1) {
        laik_append_range(r, fromTask, s, tag, 0);
        return;
    }

    // determine dimension with largest width
    int splitDim = 0;
    uint64_t width, w;
    width = s->to.i[0] - s->from.i[0];
    if (p->space->dims > 1) {
        w = s->to.i[1] - s->from.i[1];
        if (w > width) {
            width = w;
            splitDim = 1;
        }
        if (p->space->dims > 2) {
            w = s->to.i[2] - s->from.i[2];
            if (w > width) {
                width = w;
                splitDim = 2;
            }
        }
    }
    assert(width > 0);
    if (width == 1) {
        laik_append_range(r, fromTask, s, tag, 0);
        return;
    }

    // split set of tasks and width into two parts, do recursion
    int midTask = (fromTask + toTask)/2;
    w = width * (midTask-fromTask) / (toTask - fromTask);
    Laik_Range s1 = *s, s2 = *s;
    s1.to.i[splitDim] = s->from.i[splitDim] + w;
    s2.from.i[splitDim] = s->from.i[splitDim] + w;
    doBisection(r, p, &s1, fromTask, midTask);
    doBisection(r, p, &s2, midTask, toTask);
}

void runBisectionPartitioner(Laik_RangeReceiver* r, Laik_PartitionerParams* p)
{
    doBisection(r, p, &(p->space->range), 0, p->group->size);
}

Laik_Partitioner* laik_new_bisection_partitioner()
{
    return laik_new_partitioner("bisection", runBisectionPartitioner, 0, 0);
}


//-------------------------------------------------------------------
// 3d grid partitioner

typedef struct _Laik_GridPartitionerData {
    int xblocks, yblocks, zblocks;
} Laik_GridPartitionerData;


void runGridPartitioner(Laik_RangeReceiver* r, Laik_PartitionerParams* p)
{
    Laik_GridPartitionerData* data;
    data = (Laik_GridPartitionerData*) p->partitioner->data;
    assert(data);
    int tag = 1;

    assert(p->space->dims == 3);
    int blocks = data->xblocks * data->yblocks * data->zblocks;
    assert(p->group->size >= blocks);

    Laik_Range* ss = &(p->space->range);
    double xStep = (ss->to.i[0] - ss->from.i[0]) / (double) data->xblocks;
    double yStep = (ss->to.i[1] - ss->from.i[1]) / (double) data->yblocks;
    double zStep = (ss->to.i[2] - ss->from.i[2]) / (double) data->zblocks;

    Laik_Range range;
    int64_t from, to;

    range.space = p->space;
    int task = 0;
    for(int z = 0; z < data->zblocks; z++) {
        from = ss->from.i[2] + z * zStep;
        to   = ss->from.i[2] + (z+1) * zStep;
        if (from == to) continue;
        if (to > ss->to.i[2]) to = ss->to.i[2];
        range.from.i[2] = from;
        range.to.i[2]   = to;

        for(int y = 0; y < data->yblocks; y++) {
            from = ss->from.i[1] + y * yStep;
            to   = ss->from.i[1] + (y+1) * yStep;
            if (from == to) continue;
            if (to > ss->to.i[1]) to = ss->to.i[1];
            range.from.i[1] = from;
            range.to.i[1]   = to;

            for(int x = 0; x < data->xblocks; x++) {
                from = ss->from.i[0] + x * xStep;
                to   = ss->from.i[0] + (x+1) * xStep;
                if (from == to) continue;
                if (to > ss->to.i[0]) to = ss->to.i[0];
                range.from.i[0] = from;
                range.to.i[0]   = to;

                laik_append_range(r, task, &range, tag, 0);
                task++;
                if (task == p->group->size) return;
            }
        }
    }
}

Laik_Partitioner*
laik_new_grid_partitioner(int xblocks, int yblocks, int zblocks)
{
    Laik_GridPartitionerData* data;
    data = malloc(sizeof(Laik_GridPartitionerData));
    if (!data) {
        laik_panic("Out of memory allocating Laik_GridPartitionerData object");
        exit(1); // not actually needed, laik_panic never returns
    }

    data->xblocks = xblocks;
    data->yblocks = yblocks;
    data->zblocks = zblocks;

    return laik_new_partitioner("grid", runGridPartitioner, data, 0);
}



//-------------------------------------------------------------------
// block partitioner: split one dimension of space into blocks
//
// this partitioner supports:
// - index-wise weighting: give each task indexes with similar weight sum
// - task-wise weighting: scaling factor, allowing load-balancing
//
// when distributing indexes, a given number of rounds is done over tasks,
// defaulting to 1 (see cycle parameter).

typedef struct _Laik_BlockPartitionerData Laik_BlockPartitionerData;
struct _Laik_BlockPartitionerData {
     // dimension to partition, only supports 1d partitionings
    int pdim;

    // how many cycles (results in so many slics per task)
    int cycles;

    // weighted partitioning (Block) uses callbacks
    Laik_GetIdxWeight_t getIdxW;
    Laik_GetTaskWeight_t getTaskW;
    const void* userData;
};

void runBlockPartitioner(Laik_RangeReceiver* r, Laik_PartitionerParams* p)
{
    Laik_BlockPartitionerData* data;
    data = (Laik_BlockPartitionerData*) p->partitioner->data;
    assert(data);
    Laik_Space* s = p->space;
    Laik_Range range = s->range;

    int count = p->group->size;
    int pdim = data->pdim;
    int64_t size = s->range.to.i[pdim] - s->range.from.i[pdim];
    assert(size > 0);

    Laik_Index idx;
    double totalW;
    if (data && data->getIdxW) {
        // element-wise weighting
        totalW = 0.0;
        laik_index_init(&idx, 0, 0, 0);
        for(int64_t i = 0; i < size; i++) {
            idx.i[pdim] = i + s->range.from.i[pdim];
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

    range.from.i[pdim] = s->range.from.i[pdim];
    for(int64_t i = 0; i < size; i++) {
        if (data && data->getIdxW) {
            idx.i[pdim] = i + s->range.from.i[pdim];
            w += (data->getIdxW)(&idx, data->userData);
        }
        else
            w += 1.0;

        while (w >= perPart * taskW) {
            w = w - (perPart * taskW);
            if ((task+1 == count) && (cycle+1 == cycles)) break;
            range.to.i[pdim] = i + s->range.from.i[pdim];
            if (range.from.i[pdim] < range.to.i[pdim])
                laik_append_range(r, task, &range, 0, 0);
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

            // start new range
            range.from.i[pdim] = i + s->range.from.i[pdim];
        }
        if ((task+1 == count) && (cycle+1 == cycles)) break;
    }
    assert((task+1 == count) && (cycle+1 == cycles));
    range.to.i[pdim] = s->range.to.i[pdim];
    laik_append_range(r, task, &range, 0, 0);
}


Laik_Partitioner* laik_new_block_partitioner(int pdim, int cycles,
                                             Laik_GetIdxWeight_t ifunc,
                                             Laik_GetTaskWeight_t tfunc,
                                             const void* userData)
{
    Laik_BlockPartitionerData* data;
    data = malloc(sizeof(Laik_BlockPartitionerData));
    if (!data) {
        laik_panic("Out of memory allocating Laik_BlockPartitionerData object");
        exit(1); // not actually needed, laik_panic never returns
    }

    data->pdim = pdim;
    data->cycles = cycles;
    data->getIdxW = ifunc;
    data->userData = userData;
    data->getTaskW = tfunc;

    return laik_new_partitioner("block", runBlockPartitioner, data, 0);
}

Laik_Partitioner* laik_new_block_partitioner1()
{
    return laik_new_block_partitioner(0, 1, 0, 0, 0);
}

Laik_Partitioner* laik_new_block_partitioner_iw1(Laik_GetIdxWeight_t f,
                                                 const void* userData)
{
    return laik_new_block_partitioner(0, 1, f, 0, userData);
}

Laik_Partitioner* laik_new_block_partitioner_tw1(Laik_GetTaskWeight_t f,
                                                 const void* userData)
{
    return laik_new_block_partitioner(0, 1, 0, f, userData);
}

void laik_set_index_weight(Laik_Partitioner* pr, Laik_GetIdxWeight_t f,
                           const void* userData)
{
    assert(pr->run == runBlockPartitioner);

    Laik_BlockPartitionerData* data;
    data = (Laik_BlockPartitionerData*) pr->data;

    data->getIdxW = f;
    data->userData = userData;
}

void laik_set_task_weight(Laik_Partitioner* pr, Laik_GetTaskWeight_t f,
                          const void* userData)
{
    assert(pr->run == runBlockPartitioner);

    Laik_BlockPartitionerData* data;
    data = (Laik_BlockPartitionerData*) pr->data;

    data->getTaskW = f;
    data->userData = userData;
}

void laik_set_cycle_count(Laik_Partitioner* pr, int cycles)
{
    assert(pr->run == runBlockPartitioner);

    Laik_BlockPartitionerData* data;
    data = (Laik_BlockPartitionerData*) pr->data;

    if ((cycles < 0) || (cycles>10)) cycles = 1;
    data->cycles = cycles;
}



// Incremental partitioner: reassign
// redistribute indexes from tasks to be removed

typedef struct {
    Laik_Group* newg; // new group to re-distribute old partitioning

    Laik_GetIdxWeight_t getIdxW; // application-specified weights
    const void* userData;
} ReassignData;



void runReassignPartitioner(Laik_RangeReceiver* rr, Laik_PartitionerParams* p)
{
    ReassignData* data = (ReassignData*) p->partitioner->data;
    Laik_Group* newg = data->newg;

    // there must be old borders
    Laik_Partitioning* oldP = p->other;
    assert(oldP);
    // TODO: only works if parent of new group is used in oldP
    assert(newg->parent == oldP->group);
    // only 1d for now
    assert(oldP->space->dims == 1);

    // total weight sum of indexes to redistribute
    Laik_Index idx;
    laik_index_init(&idx, 0, 0, 0);
    double totalWeight = 0.0;
    int rangeCount = laik_partitioning_rangecount(oldP);
    for(int i = 0; i < rangeCount; i++) {
        Laik_TaskRange* ts = laik_partitioning_get_taskrange(oldP, i);
        int task = laik_taskrange_get_task(ts);
        if (newg->fromParent[task] >= 0) continue;

        const Laik_Range* s = laik_taskrange_get_range(ts);
        int64_t from = s->from.i[0];
        int64_t to   = s->to.i[0];

        if (data->getIdxW) {
            for(int64_t i = from; i < to; i++) {
                idx.i[0] = i;
                totalWeight += (data->getIdxW)(&idx, data->userData);
            }
        }
        else
            totalWeight += (double) (to - from);
    }

    // weight to re-distribute to each remaining task
    double weightPerTask = totalWeight / newg->size;
    double weight = 0;
    int curTask = 0; // task in new group which gets the next indexes

    laik_log(1, "reassign: re-distribute weight %.3f to %d tasks (%.3f per task)",
             totalWeight, newg->size, weightPerTask);

    Laik_Range range;
    range.space = p->space;
    for(int rangeNo = 0; rangeNo < rangeCount; rangeNo++) {
        Laik_TaskRange* ts = laik_partitioning_get_taskrange(oldP, rangeNo);
        int origTask = laik_taskrange_get_task(ts);
        const Laik_Range* r = laik_taskrange_get_range(ts);
        if (newg->fromParent[origTask] >= 0) {
            // move over to new borders
            laik_log(1, "reassign: take over range %d of task %d "
                     "(new task %d, indexes [%lld;%lld[)",
                     rangeNo, origTask, newg->fromParent[origTask],
                     (long long int) r->from.i[0],
                     (long long int) r->to.i[0]);

            laik_append_range(rr, origTask, r, 0, 0);
            continue;
        }

        // re-distribute
        int64_t from = r->from.i[0];
        int64_t to =   r->to.i[0];

        range.from.i[0] = from;
        for(int64_t i = from; i < to; i++) {
            if (data->getIdxW) {
                idx.i[0] = i;
                weight += (data->getIdxW)(&idx, data->userData);
            }
            else
                weight += (double) 1.0;
            // add range if weight too large, but only if not already last task
            if ((weight >= weightPerTask) && (curTask < newg->size)) {
                weight -= weightPerTask;
                range.to.i[0] = i + 1;
                laik_append_range(rr, newg->toParent[curTask], &range, 0, 0);

                laik_log(1, "reassign: re-distribute [%lld;%lld[ "
                         "of range %d to task %d (new task %d)",
                         (long long int) range.from.i[0],
                         (long long int) range.to.i[0], rangeNo,
                         newg->toParent[curTask], curTask);

                // start new range
                range.from.i[0] = i + 1;
                curTask++;
                if (curTask == newg->size) {
                    // left over indexes always go to last task ID
                    // this can happen if weights are 0 for last indexes
                    curTask--;
                }
            }
        }
        if (range.from.i[0] < to) {
            range.to.i[0] = to;
            laik_append_range(rr, newg->toParent[curTask], &range, 0, 0);

            laik_log(1, "reassign: re-distribute remaining [%lld;%lld[ "
                     "of range %d to task %d (new task %d)",
                     (long long int) range.from.i[0],
                     (long long int) range.to.i[0], rangeNo,
                     newg->toParent[curTask], curTask);
        }
    }
}

Laik_Partitioner*
laik_new_reassign_partitioner(Laik_Group* newg,
                              Laik_GetIdxWeight_t getIdxW,
                              const void* userData)
{
    ReassignData* data = malloc(sizeof(ReassignData));
    if (!data) {
        laik_panic("Out of memory allocating ReassignData object");
        exit(1); // not actually needed, laik_panic never returns
    }

    data->newg = newg;
    data->getIdxW = getIdxW;
    data->userData = userData;

    return laik_new_partitioner("reassign", runReassignPartitioner,
                                data, 0);
}

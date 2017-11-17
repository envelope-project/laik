/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>

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


// Simple partitioners

// all-partitioner: all tasks have access to all indexes

void runAllPartitioner(Laik_Partitioner* pr,
                       Laik_BorderArray* ba, Laik_BorderArray* oldBA)
{
    Laik_Space* space = ba->space;
    Laik_Group* g = ba->group;

    for(int task = 0; task < g->size; task++) {
        laik_append_slice(ba, task, &(space->s), 0, 0);
    }
}

Laik_Partitioner* laik_new_all_partitioner()
{
    return laik_new_partitioner("all", runAllPartitioner, 0, 0);
}

// master-partitioner: only task 0 has access to all indexes

void runMasterPartitioner(Laik_Partitioner* pr,
                          Laik_BorderArray* ba, Laik_BorderArray* oldBA)
{
    // only full slice for master
    laik_append_slice(ba, 0, &(ba->space->s), 0, 0);
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

void runCopyPartitioner(Laik_Partitioner* pr,
                        Laik_BorderArray* ba, Laik_BorderArray* otherBA)
{
    Laik_CopyPartitionerData* data = (Laik_CopyPartitionerData*) pr->data;
    assert(data);
    int fromDim = data->fromDim;
    int toDim = data->toDim;

    assert(otherBA->group == ba->group); // must use same task group
    assert((fromDim >= 0) && (fromDim < otherBA->space->dims));
    assert((toDim >= 0) && (toDim < ba->space->dims));

    for(int i = 0; i < otherBA->count; i++) {
        Laik_Slice slc = ba->space->s;
        slc.from.i[toDim] = otherBA->tslice[i].s.from.i[fromDim];
        slc.to.i[toDim] = otherBA->tslice[i].s.to.i[fromDim];
        laik_append_slice(ba, otherBA->tslice[i].task, &slc, 0, 0);
    }
}

Laik_Partitioner* laik_new_copy_partitioner(int fromDim, int toDim)
{
    Laik_CopyPartitionerData* data;
    int dsize = sizeof(Laik_CopyPartitionerData);
    data = malloc(dsize);
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
void runCornerHaloPartitioner(Laik_Partitioner* pr,
                              Laik_BorderArray* ba, Laik_BorderArray* otherBA)
{
    assert(otherBA->group == ba->group); // must use same task group
    assert(otherBA->space == ba->space);

    int dims = ba->space->dims;
    int d = *((int*) pr->data);

    // take all slices and extend them if possible
    for(int i = 0; i < otherBA->count; i++) {
        Laik_Slice slc = ba->space->s;
        Laik_Index* from = &(otherBA->tslice[i].s.from);
        Laik_Index* to = &(otherBA->tslice[i].s.to);

        if (from->i[0] > slc.from.i[0] + d)
            slc.from.i[0] = from->i[0] - d;
        if (to->i[0] < slc.to.i[0] - d)
            slc.to.i[0] = to->i[0] + d;

        if (dims > 1) {
            if (from->i[1] > slc.from.i[1] + d)
                slc.from.i[1] = from->i[1] - d;
            if (to->i[1] < slc.to.i[1] - d)
                slc.to.i[1] = to->i[1] + d;

            if (dims > 2) {
                if (from->i[2] > slc.from.i[2] + d)
                    slc.from.i[2] = from->i[2] - d;
                if (to->i[2] < slc.to.i[2] - d)
                    slc.to.i[2] = to->i[2] + d;
            }
        }
        laik_append_slice(ba, otherBA->tslice[i].task, &slc, 0, 0);
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


// halo partitioner: extend borders of other partitioning
// excluding corners, e.g. for 5-point 2d stencil.
// this creates multiple slices for each original slice, and uses tags
// to mark the halos of a slice to go into same mapping
void runHaloPartitioner(Laik_Partitioner* pr,
                        Laik_BorderArray* ba, Laik_BorderArray* otherBA)
{
    assert(otherBA->group == ba->group); // must use same task group
    assert(otherBA->space == ba->space);

    int dims = ba->space->dims;
    int d = *((int*) pr->data);

    int nextTag = 1;

    // take all slices and extend them if possible
    for(int i = 0; i < otherBA->count; i++) {
        Laik_Slice slc;
        Laik_Slice sp = ba->space->s;

        slc = otherBA->tslice[i].s;
        laik_append_slice(ba, otherBA->tslice[i].task, &slc, nextTag, 0);
        if (slc.from.i[0] > sp.from.i[0] + d) {
            slc.to.i[0] = slc.from.i[0];
            slc.from.i[0] -= d;
            laik_append_slice(ba, otherBA->tslice[i].task, &slc, nextTag, 0);
        }
        slc = otherBA->tslice[i].s;
        if (slc.to.i[0] < sp.to.i[0] - d) {
            slc.from.i[0] = slc.to.i[0];
            slc.to.i[0] += d;
            laik_append_slice(ba, otherBA->tslice[i].task, &slc, nextTag, 0);
        }

        if (dims > 1) {
            slc = otherBA->tslice[i].s;
            if (slc.from.i[1] > sp.from.i[1] + d) {
                slc.to.i[1] = slc.from.i[1];
                slc.from.i[1] -= d;
                laik_append_slice(ba, otherBA->tslice[i].task, &slc, nextTag, 0);
            }
            slc = otherBA->tslice[i].s;
            if (slc.to.i[1] < sp.to.i[1] - d) {
                slc.from.i[1] = slc.to.i[1];
                slc.to.i[1] += d;
                laik_append_slice(ba, otherBA->tslice[i].task, &slc, nextTag, 0);
            }

            if (dims > 2) {
                slc = otherBA->tslice[i].s;
                if (slc.from.i[2] > sp.from.i[2] + d) {
                    slc.to.i[2] = slc.from.i[2];
                    slc.from.i[2] -= d;
                    laik_append_slice(ba, otherBA->tslice[i].task, &slc, nextTag, 0);
                }
                slc = otherBA->tslice[i].s;
                if (slc.to.i[2] < sp.to.i[2] - d) {
                    slc.from.i[2] = slc.to.i[2];
                    slc.to.i[2] += d;
                    laik_append_slice(ba, otherBA->tslice[i].task, &slc, nextTag, 0);
                }
            }
        }

        nextTag++; // could reset to 1 when going to next task
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

// recursive helper: distribute slice <s> to tasks in range [fromTask;toTask[
static void doBisection(Laik_BorderArray* ba,
                        Laik_Slice* s, int fromTask, int toTask)
{
    assert(toTask > fromTask);
    if (toTask - fromTask == 1) {
        laik_append_slice(ba, fromTask, s, 0, 0);
        return;
    }

    // determine dimension with largest width
    int splitDim = 0;
    uint64_t width, w;
    width = s->to.i[0] - s->from.i[0];
    if (ba->space->dims > 1) {
        w = s->to.i[1] - s->from.i[1];
        if (w > width) {
            width = w;
            splitDim = 1;
        }
        if (ba->space->dims > 2) {
            w = s->to.i[2] - s->from.i[2];
            if (w > width) {
                width = w;
                splitDim = 2;
            }
        }
    }
    assert(width > 0);
    if (width == 1) {
        laik_append_slice(ba, fromTask, s, 0, 0);
        return;
    }

    // split set of tasks and width into two parts, do recursion
    int midTask = (fromTask + toTask)/2;
    w = width * (midTask-fromTask) / (toTask - fromTask);
    Laik_Slice s1 = *s, s2 = *s;
    s1.to.i[splitDim] = s->from.i[splitDim] + w;
    s2.from.i[splitDim] = s->from.i[splitDim] + w;
    doBisection(ba, &s1, fromTask, midTask);
    doBisection(ba, &s2, midTask, toTask);
}

void runBisectionPartitioner(Laik_Partitioner* pr,
                             Laik_BorderArray* ba, Laik_BorderArray* otherBA)
{
    doBisection(ba, &(ba->space->s), 0, ba->group->size);
}

Laik_Partitioner* laik_new_bisection_partitioner()
{
    return laik_new_partitioner("bisection", runBisectionPartitioner, 0, 0);
}


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

void runBlockPartitioner(Laik_Partitioner* pr,
                         Laik_BorderArray* ba, Laik_BorderArray* oldBA)
{
    Laik_BlockPartitionerData* data;
    data = (Laik_BlockPartitionerData*) pr->data;

    Laik_Space* s = ba->space;
    Laik_Slice slc = s->s;

    int count = ba->group->size;
    int pdim = data->pdim;
    uint64_t size = s->s.to.i[pdim] - s->s.from.i[pdim];

    Laik_Index idx;
    double totalW;
    if (data && data->getIdxW) {
        // element-wise weighting
        totalW = 0.0;
        laik_set_index(&idx, 0, 0, 0);
        for(uint64_t i = 0; i < size; i++) {
            idx.i[pdim] = i + s->s.from.i[pdim];
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

    slc.from.i[pdim] = s->s.from.i[pdim];
    for(uint64_t i = 0; i < size; i++) {
        if (data && data->getIdxW) {
            idx.i[pdim] = i + s->s.from.i[pdim];
            w += (data->getIdxW)(&idx, data->userData);
        }
        else
            w += 1.0;

        while (w >= perPart * taskW) {
            w = w - (perPart * taskW);
            if ((task+1 == count) && (cycle+1 == cycles)) break;
            slc.to.i[pdim] = i + s->s.from.i[pdim];
            if (slc.from.i[pdim] < slc.to.i[pdim])
                laik_append_slice(ba, task, &slc, 0, 0);
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
            slc.from.i[pdim] = i + s->s.from.i[pdim];
        }
        if ((task+1 == count) && (cycle+1 == cycles)) break;
    }
    assert((task+1 == count) && (cycle+1 == cycles));
    slc.to.i[pdim] = s->s.to.i[pdim];
    laik_append_slice(ba, task, &slc, 0, 0);
}


Laik_Partitioner* laik_new_block_partitioner(int pdim, int cycles,
                                             Laik_GetIdxWeight_t ifunc,
                                             Laik_GetTaskWeight_t tfunc,
                                             const void* userData)
{
    Laik_BlockPartitionerData* data;
    int dsize = sizeof(Laik_BlockPartitionerData);
    data = malloc(dsize);
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



void runReassignPartitioner(Laik_Partitioner* pr,
                            Laik_BorderArray* ba, Laik_BorderArray* oldBA)
{
    ReassignData* data = (ReassignData*) pr->data;
    Laik_Group* newg = data->newg;

    // there must be old borders
    assert(oldBA);
    // TODO: only works if parent of new group is used in oldBA
    assert(newg->parent == oldBA->group);
    // only 1d for now
    assert(oldBA->space->dims == 1);

    // total weight sum of indexes to redistribute
    Laik_Index idx;
    laik_set_index(&idx, 0, 0, 0);
    double totalWeight = 0.0;
    for(int sliceNo = 0; sliceNo < oldBA->count; sliceNo++) {
        int task = oldBA->tslice[sliceNo].task;
        if (newg->fromParent[task] >= 0) continue;

        int from = oldBA->tslice[sliceNo].s.from.i[0];
        int to   = oldBA->tslice[sliceNo].s.to.i[0];

        if (data->getIdxW) {
            for(uint64_t i = from; i < to; i++) {
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

    Laik_Slice slc;
    for(int sliceNo = 0; sliceNo < oldBA->count; sliceNo++) {
        int origTask = oldBA->tslice[sliceNo].task;
        if (newg->fromParent[origTask] >= 0) {
            // move over to new borders
            laik_log(1, "reassign: take over slice %d of task %d "
                     "(new task %d, indexes [%d;%d[)",
                     sliceNo, origTask, newg->fromParent[origTask],
                     oldBA->tslice[sliceNo].s.from.i[0],
                     oldBA->tslice[sliceNo].s.to.i[0]);

            laik_append_slice(ba, origTask, &(oldBA->tslice[sliceNo].s), 0, 0);
            continue;
        }

        // re-distribute
        int from = oldBA->tslice[sliceNo].s.from.i[0];
        int to = oldBA->tslice[sliceNo].s.to.i[0];

        slc.from.i[0] = from;
        for(uint64_t i = from; i < to; i++) {
            if (data->getIdxW) {
                idx.i[0] = i;
                weight += (data->getIdxW)(&idx, data->userData);
            }
            else
                weight += (double) 1.0;
            // add slice if weight too large, but only if not already last task
            if ((weight >= weightPerTask) && (curTask < newg->size)) {
                weight -= weightPerTask;
                slc.to.i[0] = i + 1;
                laik_append_slice(ba, newg->toParent[curTask], &slc, 0, 0);

                laik_log(1, "reassign: re-distribute [%d;%d[ "
                         "of slice %d to task %d (new task %d)",
                         slc.from.i[0], slc.to.i[0], sliceNo,
                         newg->toParent[curTask], curTask);

                // start new slice
                slc.from.i[0] = i + 1;
                curTask++;
                if (curTask == newg->size) {
                    // left over indexes always go to last task ID
                    // this can happen if weights are 0 for last indexes
                    curTask--;
                }
            }
        }
        if (slc.from.i[0] < to) {
            slc.to.i[0] = to;
            laik_append_slice(ba, newg->toParent[curTask], &slc, 0, 0);

            laik_log(1, "reassign: re-distribute remaining [%d;%d[ "
                     "of slice %d to task %d (new task %d)",
                     slc.from.i[0], slc.to.i[0], sliceNo,
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

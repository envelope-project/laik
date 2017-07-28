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
        laik_set_index(&(slc.from), 0, 0, 0);
        laik_set_index(&(slc.to), s->size[0], s->size[1], s->size[2]);
        laik_append_slice(ba, task, &slc);
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
    laik_set_index(&(slc.from), 0, 0, 0);
    laik_set_index(&(slc.to), s->size[0], s->size[1], s->size[2]);
    laik_append_slice(ba, 0, &slc);
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

typedef struct _Laik_CopyPartitionerData Laik_CopyPartitionerData;
struct _Laik_CopyPartitionerData {
    int fromDim, toDim; // only supports 1d partitionings
    Laik_Partitioning* base;
};

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
        laik_set_index(&(slc.from), 0, 0, 0);
        laik_set_index(&(slc.to), s->size[0], s->size[1], s->size[2]);
        slc.from.i[toDim] = baseBorders->tslice[i].s.from.i[fromDim];
        slc.to.i[toDim] = baseBorders->tslice[i].s.to.i[fromDim];
        laik_append_slice(ba, baseBorders->tslice[i].task, &slc);
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
    Laik_Slice slc;
    laik_set_index(&(slc.from), 0, 0, 0);
    laik_set_index(&(slc.to), s->size[0], s->size[1], s->size[2]);

    int count = ba->group->size;
    int pdim = data->pdim;
    uint64_t size = s->size[pdim];

    Laik_Index idx;
    double totalW;
    if (data && data->getIdxW) {
        // element-wise weighting
        totalW = 0.0;
        laik_set_index(&idx, 0, 0, 0);
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
                laik_append_slice(ba, task, &slc);
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
    laik_append_slice(ba, task, &slc);
}


Laik_Partitioner* laik_new_block_partitioner(int pdim, int cycles,
                                             Laik_GetIdxWeight_t ifunc,
                                             Laik_GetTaskWeight_t tfunc,
                                             const void* userData)
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
    Laik_BlockPartitionerData* data;
    data = (Laik_BlockPartitionerData*) pr->data;

    data->getIdxW = f;
    data->userData = userData;
}

void laik_set_task_weight(Laik_Partitioner* pr, Laik_GetTaskWeight_t f,
                          const void* userData)
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

            laik_append_slice(ba, origTask, &(oldBA->tslice[sliceNo].s));
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
                laik_append_slice(ba, newg->toParent[curTask], &slc);

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
            laik_append_slice(ba, newg->toParent[curTask], &slc);

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
    ReassignData* data = (ReassignData*) malloc(sizeof(ReassignData));
    data->newg = newg;
    data->getIdxW = getIdxW;
    data->userData = userData;

    return laik_new_partitioner("reassign", runReassignPartitioner,
                                (void*) data);
}

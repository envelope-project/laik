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


#ifndef LAIK_SPACE_INTERNAL_H
#define LAIK_SPACE_INTERNAL_H

#include <laik.h>     // for Laik_Index, Laik_Partitioning
#include <stdbool.h>  // for bool
#include <stdint.h>   // for int64_t

void laik_add_index(Laik_Index* res, Laik_Index* src1, Laik_Index* src2);
void laik_sub_index(Laik_Index* res, const Laik_Index* src1, const Laik_Index* src2);



struct _Laik_Space {
    char* name; // for debugging
    int id;     // for debugging

    int dims;
    Laik_Slice s; // defines the valid indexes in this space

    Laik_Instance* inst;
    Laik_Space* nextSpaceForInstance; // for list of spaces used in instance
};

struct _Laik_Partitioner {
    const char* name;
    laik_run_partitioner_t run;
    Laik_PartitionerFlag flags;
    void* data; // partitioner specific data
};




// the output of a partitioner is a Laik_Partitioning

// A TaskSlice is used in partitioning to map a slice to a task.

// generic task slice
// the tag is a hint for the data layer: if >0, slices with same tag
// go into same mapping
typedef struct _Laik_TaskSlice_Gen {
    int task;
    Laik_Slice s;

    int tag;
    void* data;

    // calculated from <tag> after partitioner run
    int mapNo;
} Laik_TaskSlice_Gen;

// for single-index slices in 1d
typedef struct _Laik_TaskSlice_Single1d {
    int task;
    int64_t idx;
} Laik_TaskSlice_Single1d;

// generic reference to a task slice by slice index in a partitioning
struct _Laik_TaskSlice {
    Laik_Partitioning* p;
    int no;
};

// if a slice filter is installed for a new created partitioning, for
// each slice added by the partitioner, the filter is called to check
// whether the slice actually should be stored
typedef bool
(*laik_pfilter_t)(Laik_Partitioning*, int task, Laik_Slice* s);

struct _Laik_Partitioning {
    int id;
    char* name;

    Laik_Group* group; // slices are assigned to processes in this group
    Laik_Space* space; // slices are sub-ranges of this space

    int capacity;  // slices allocated
    int count;     // slices used

    Laik_TaskSlice_Gen* tslice; // slice array
    Laik_TaskSlice_Single1d* tss1d; // specific array used during collection

    // calculated on freezing
    int* off;       // offsets into slices, ordered by rank
    int myMapCount; // number of mappings needed for slices of own process
    int* myMapOff;  // offsets into own slices for same mapping

    // optional: partitioner to be called
    Laik_Partitioner* partitioner; // if set: creating partitioner

    // base partitioning, used with partitioner or chained partitionings
    Laik_Partitioning* other;

    // if set, this maps ranks from <other> to <group>
    //   used by laik_new_migrated_partitioning()
    int* fromOther;


    // slice filters and consumers

    // if set: call this function for each slice
    laik_pfilter_t filter;

    // a filter can forward slices to this partitioning
    Laik_Partitioning* consumer;

    // my filter: if true, only store slices for own process
    bool myfilter;

    // partition intersection filter:
    // if set, only slices intersecting own slices from these partitionings
    // are stored. max of 2 partitionings can be given
    // (used in laik_calc_transition for reduced memory consumption)
    Laik_Partitioning *pfilter1, *pfilter2;

    // allow to cache a partitioning which stores slices which intersect with
    // own slices of this partitioning and another one set as
    // "intersecting->pfilter1/2"
    // (used in laik_calc_transition for reduced memory consumption)
    Laik_Partitioning* intersecting;
};

void laik_clear_partitioning(Laik_Partitioning* p);
void laik_free_partitioning(Laik_Partitioning* p);
void laik_updateMyMapOffsets(Laik_Partitioning* p);



//
// Laik_Transition
//

// Sub-structures of Laik_Transition.
// Notes:
// - slice indexes are always global
// - there are separate slice vs. map numbers, as multiple slices may go to
//   one mapping

// slice staying local
struct localTOp {
    Laik_Slice slc;
    int fromSliceNo, toSliceNo;
    int fromMapNo, toMapNo;
};

// slice to be initialized
struct initTOp {
    Laik_Slice slc;
    int sliceNo, mapNo;
    Laik_ReductionOperation redOp;
};

// slice to send to a remote task
struct sendTOp {
    Laik_Slice slc;
    int sliceNo, mapNo;
    int toTask;
};

// slice to receive from a remote task
struct recvTOp {
    Laik_Slice slc;
    int sliceNo, mapNo;
    int fromTask;
};

// referenced in reduction operation within a transition
typedef struct _TaskGroup {
    int count;
    int* task; // sorted list
} TaskGroup;

// slice to reduce
struct redTOp {
    Laik_Slice slc;
    Laik_ReductionOperation redOp;
    int inputGroup, outputGroup; // references into group list, or -1: all
    int myInputSliceNo, myOutputSliceNo;
    int myInputMapNo, myOutputMapNo;
};

// transition flags
#define LAIK_TF_KEEP_REDUCTIONS 1

struct _Laik_Transition {
    // data identifying this transition
    int flags;
    Laik_Space* space;
    Laik_Group* group;
    Laik_Partitioning *fromPartitioning, *toPartitioning;
    Laik_DataFlow flow;
    Laik_ReductionOperation redOp;

    int dims;
    int actionCount;

    // local slices staying local;
    // may need copy when different from/to mappings are used
    int localCount;
    struct localTOp *local;

    // local slices that should be initialized;
    // the value depends on the reduction type (neutral element)
    int initCount;
    struct initTOp *init;

    // slices to send to other task
    int sendCount;
    struct sendTOp *send;

    // slices to receive from other task
    int recvCount;
    struct recvTOp *recv;

    // slices to reduce
    int redCount;
    struct redTOp *red;

    // sub-groups of task group referenced by reduction operations
    int subgroupCount;
    TaskGroup *subgroup;
};

// same as laik_calc_transition without logging
Laik_Transition* do_calc_transition(Laik_Space* space,
                                    Laik_Partitioning* fromP, Laik_Partitioning* toP,
                                    Laik_DataFlow flow, Laik_ReductionOperation redOp);

// return size of task group with ID <subgroup> in transition <t>
int laik_trans_groupCount(Laik_Transition* t, int subgroup);

// return task ID of <i>'th task in group with ID <subgroup> in transition <t>
int laik_trans_taskInGroup(Laik_Transition* t, int subgroup, int i);

// true if a task is part of the group with ID <subgroup> in transition <t>
bool laik_trans_isInGroup(Laik_Transition* t, int subgroup, int task);


// initialize the LAIK space module, called from laik_new_instance
void laik_space_init(void);

#endif // LAIK_SPACE_INTERNAL_H

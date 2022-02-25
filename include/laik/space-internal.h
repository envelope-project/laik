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
    Laik_Range range; // defines the valid indexes in this space

    Laik_KVStore* kvs; // attached to this store if non-null

    Laik_Instance* inst;
    Laik_Space* nextSpaceForInstance; // for list of spaces used in instance
};

// internal: space store
Laik_KVStore* laik_spacestore(Laik_Instance* i);
void laik_spacestore_set(Laik_Space* s);
Laik_Space* laik_spacestore_get(Laik_Instance* i, char* name);
void laik_sync_spaces(Laik_Instance* i);


struct _Laik_Partitioner {
    const char* name;
    laik_run_partitioner_t run;
    Laik_PartitionerFlag flags;
    void* data; // partitioner specific data
};

// context during a partitioner run, to filter and forward ranges
struct _Laik_RangeReceiver {
    Laik_RangeList* list;
    Laik_RangeFilter* filter;
    Laik_PartitionerParams* params;
};


// a TaskRange is used in a partitioning to assign a range to a task

// generic task range
// the tag is a hint for the data layer: if >0, ranges with same tag
// go into same memory mapping
typedef struct _Laik_TaskRange_Gen {
    int task;
    Laik_Range range;

    int tag;
    void* data;

    // calculated from <tag> after partitioner run
    int mapNo;
} Laik_TaskRange_Gen;

// for single-index ranges in 1d
typedef struct _Laik_TaskRange_Single1d {
    int task;
    int64_t idx;
} Laik_TaskRange_Single1d;

// generic reference to a task range by indexing into a range list
struct _Laik_TaskRange {
    Laik_RangeList* list;
    int no;
};


// an ordered sequence of ranges assigned to task ids
// ordered by task id, then mapping id, then index ordering
struct _Laik_RangeList {
    Laik_Space* space;
    unsigned int tid_count; // size of <off> array

    unsigned int capacity;   // ranges allocated
    unsigned int count;      // ranges used

    Laik_TaskRange_Gen* trange; // range array
    Laik_TaskRange_Single1d* tss1d; // specific array used during collection

    // calculated on freezing
    unsigned int* off;       // offsets into ranges, ordered by task id

    // for fast access to ranges within mappings from a task with given id
    int map_tid;  // typically used for "own" task id
    unsigned int map_count; // number of mappings needed for ranges of <maptask>
    unsigned int* map_off;  // offsets into own ranges for same mapping
};


// if a range filter is installed for a new created partitioning, for
// each range added by the partitioner, the filter is called to check
// whether the range actually should be stored
typedef bool
    (*laik_filterFunc_t)(Laik_RangeFilter*, int task, const Laik_Range* r);

// for intersection partitioning filter
typedef struct {
    int64_t from, to;
    Laik_TaskRange_Gen* ts;
    unsigned int len;
} PFilterPar;

// parameters for filtering ranges on a partitioner run
struct _Laik_RangeFilter {
    // if set: call this function for each range
    laik_filterFunc_t filter_func;

    // for task id filter: only store ranges for this task id (if >=0)
    int filter_tid;

    // partition intersection filter:
    // if set, only ranges intersecting own ranges from these partitionings
    // are stored. max of 2 partitionings can be given
    // (used in laik_calc_transition for reduced memory consumption)
    PFilterPar *pfilter1, *pfilter2;
};

// meta info about range lists stored with partitionings (RI = range info)
// this gets set when running partitioner with specific filter
typedef enum _RangeInfo {
    LAIK_RI_UNKNOWN = 0, // range list explicitly set
    LAIK_RI_FULL,        // run of partitioner without
    LAIK_RI_SINGLETASK,  // run of partitioner filtering ranges of a task
    LAIK_RI_INTERSECT    // run of partitioner filtering intersecting ranges
} RangeInfo;

// a partitioning can store multiple range lists created from partitioner
// runs with different filters
typedef struct _RangeList_Entry {
    RangeInfo info;
    int filter_tid; // for AI_SINGLETASK
    Laik_Partitioning* other; // for AI_INTERSECT

    Laik_RangeList* ranges;
    struct _RangeList_Entry* next;
} RangeList_Entry;

struct _Laik_Partitioning {
    int id;
    char* name;

    Laik_Group* group; // ranges are assigned to processes in this group
    Laik_Space* space; // ranges are sub-ranges of this space

    RangeList_Entry* rangeList;

    // optional: partitioner to be called
    Laik_Partitioner* partitioner; // if set: creating partitioner

    // base partitioning, used with partitioner or chained partitionings
    Laik_Partitioning* other;
};

void laik_free_partitioning(Laik_Partitioning* p);
void laik_updateMapOffsets(Laik_RangeList* list, int tid);



//
// Laik_Transition
//

// Sub-structures of Laik_Transition.
// Notes:
// - range indexes are always global
// - there are separate range vs. map numbers, as multiple ranges may go to
//   one mapping

// range staying local
struct localTOp {
    Laik_Range range;
    int fromRangeNo, toRangeNo;
    int fromMapNo, toMapNo;
};

// range to be initialized
struct initTOp {
    Laik_Range range;
    int rangeNo, mapNo;
    Laik_ReductionOperation redOp;
};

// range to send to a remote task
struct sendTOp {
    Laik_Range range;
    int rangeNo, mapNo;
    int toTask;
};

// range to receive from a remote task
struct recvTOp {
    Laik_Range range;
    int rangeNo, mapNo;
    int fromTask;
};

// referenced in reduction operation within a transition
typedef struct _TaskGroup {
    int count;
    int* task; // sorted list
} TaskGroup;

// range to reduce
struct redTOp {
    Laik_Range range;
    Laik_ReductionOperation redOp;
    int inputGroup, outputGroup; // references into group list, or -1: all
    int myInputRangeNo, myOutputRangeNo;
    int myInputMapNo, myOutputMapNo;
};

// transition flags
#define LAIK_TF_KEEP_REDUCTIONS 1

struct _Laik_Transition {
    int id;
    char* name;

    // data identifying this transition
    int flags;
    Laik_Space* space;
    Laik_Group* group;
    Laik_Partitioning *fromPartitioning, *toPartitioning;
    Laik_DataFlow flow;
    Laik_ReductionOperation redOp;

    int dims;
    int actionCount;

    // local ranges staying local;
    // may need copy when different from/to mappings are used
    int localCount;
    struct localTOp *local;

    // local ranges that should be initialized;
    // the value depends on the reduction type (neutral element)
    int initCount;
    struct initTOp *init;

    // ranges to send to other task
    int sendCount;
    struct sendTOp *send;

    // ranges to receive from other task
    int recvCount;
    struct recvTOp *recv;

    // ranges to reduce
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

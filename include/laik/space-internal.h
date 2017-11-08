/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 * 
 * LAIK is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU Lesser General Public License as   
 * published by the Free Software Foundation, version 3.
 *
 * LAIK is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LAIK_SPACE_INTERNAL_H_
#define _LAIK_SPACE_INTERNAL_H_

#ifndef _LAIK_INTERNAL_H_
#error "include laik-internal.h instead"
#endif

#include <stdbool.h>

void laik_set_index(Laik_Index* i, uint64_t i1, uint64_t i2, uint64_t i3);
void laik_add_index(Laik_Index* res, Laik_Index* src1, Laik_Index* src2);
void laik_sub_index(Laik_Index* res, Laik_Index* src1, Laik_Index* src2);


struct _Laik_Space {
    char* name; // for debugging
    int id;     // for debugging

    int dims;
    Laik_Slice s; // defines the valid indexes in this space

    Laik_Instance* inst;
    Laik_Space* nextSpaceForInstance; // for list of spaces used in instance

    // linked list of partitionings for this space
    Laik_Partitioning* firstPartitioningForSpace;
};

// add/remove partitioning to/from space
void laik_addPartitioningForSpace(Laik_Space* s, Laik_Partitioning* p);
void laik_removePartitioningFromSpace(Laik_Space* s, Laik_Partitioning* p);

struct _Laik_Partitioner {
    const char* name;
    laik_run_partitioner_t run;
    Laik_PartitionerFlag flags;
    void* data; // partitioner specific data
};




// the output of a partitioner is a Laik_BorderArray

// A TaskSlice is used in BorderArray to map a slice to a task.

// different internal types are used to save memory
enum { TS_Generic = 1, TS_Single1d };

struct _Laik_TaskSlice {
    int type;
    int task;
};

// generic task slice
// the tag is a hint for the data layer: if >0, slices with same tag
// go into same mapping
typedef struct _Laik_TaskSlice_Gen {
    int type;
    int task;
    Laik_Slice s;

    int tag;
    void* data;

    // calculated from <tag> after partitioner run
    int mapNo;
} Laik_TaskSlice_Gen;

// for single-index slices in 1d
typedef struct _Laik_TaskSlice_Single1d {
    int type;
    int task;
    uint64_t idx;
} Laik_TaskSlice_Single1d;

struct _Laik_BorderArray {
    Laik_Group* group; // task IDs used belong to this group
    Laik_Space* space; // slices cover this space
    int capacity;  // slices allocated
    int count;     // slices used
    int* off;      // offsets from task IDs into border array
    Laik_TaskSlice_Gen* tslice; // slice borders, may be multiple per task
    Laik_TaskSlice_Single1d* tss1d;
};

Laik_BorderArray* laik_allocBorders(Laik_Group* g, Laik_Space* s, bool useSingle1d);
void laik_clearBorderArray(Laik_BorderArray* a);
void laik_freeBorderArray(Laik_BorderArray* a);


//
// Laik_Partitioning
//

struct _Laik_Partitioning {
    char* name; // for debugging
    int id;     // for debugging

    Laik_Group* group;
    Laik_Space* space; // space to partition

    Laik_Partitioner* partitioner;
    Laik_Partitioning* base;

    // partition borders (calculated lazy)
    bool bordersValid;
    Laik_BorderArray* borders;

    // head of list of data containers with this paritioning active
    Laik_Data* firstDataForPartitioning;
    // head of list of partitionings using this one as base
    Laik_Partitioning* firstPartitioningForBase;

     // for list of partitionings using same space
    Laik_Partitioning* nextPartitioningForSpace;
    // for list of partitionings using same group
    Laik_Partitioning* nextPartitioningForGroup;
    // for list of partitionings using this one as base
    Laik_Partitioning* nextPartitioningForBase;
};

// add/remove partitioning to/from list using a given partitioning as base
void laik_addPartitioningForBase(Laik_Partitioning* base,
                                 Laik_Partitioning* p);
void laik_removePartitioningFromBase(Laik_Partitioning* base,
                                     Laik_Partitioning* p);


// add/remove data container as user to/from partitioning
void laik_addDataForPartitioning(Laik_Partitioning* p, Laik_Data* d);
void laik_removeDataFromPartitioning(Laik_Partitioning* p, Laik_Data* d);


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

// slice to reduce
struct redTOp {
    Laik_Slice slc;
    Laik_ReductionOperation redOp;
    int rootTask; // -1: all
};

struct _Laik_Transition {
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
};

// initialize the LAIK space module, called from laik_new_instance
void laik_space_init();

#endif // _LAIK_SPACE_INTERNAL_H_

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
    char* name;
    void* data; // partitioner specific data
    laik_run_partitioner_t run;
};

Laik_Partitioner* laik_new_partitioner(char* name,
                                       laik_run_partitioner_t f, void* d);

// to be used by implementations of partitioners
// the <tag> is a hint for the data layer: if >0, slices with same tag go
//  into same mapping.
// the <data> pointer is an arbitrary value which can be passed from
//  application-specific partitioners to the code processing slices.
//  LAIK provided partitioners set <data> to 0.
Laik_TaskSlice* laik_append_slice(Laik_BorderArray* a, int task, Laik_Slice* s,
                                  int tag, void* data);


// the output of a partitioner is a Laik_BorderArray

// used in BorderArray to map a slice to a task.
// the tag is a hint for the data layer: if >0, slices with same tag
// go into same mapping
struct _Laik_TaskSlice {
    int task;
    int tag;
    void* data;
    Laik_Slice s;
};

struct _Laik_BorderArray {
    Laik_Group* group; // task IDs used belong to this group
    Laik_Space* space; // slices cover this space
    int capacity;  // slices allocated
    int count;     // slices used
    int* off;      // offsets from task IDs into border array
    Laik_TaskSlice* tslice; // slice borders, may be multiple per task
};

Laik_BorderArray* laik_allocBorders(Laik_Group* g, Laik_Space* s, int capacity);
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

// sub-structures of Laik_Transition

// slice staying local
struct localTOp {
    Laik_Slice slc;
    int fromSliceNo, toSliceNo;
};

// slice to be initialized
struct initTOp {
    Laik_Slice slc;
    int sliceNo;
    Laik_ReductionOperation redOp;
};

// slice to send to a remote task
struct sendTOp {
    Laik_Slice slc;
    int sliceNo;
    int toTask;
};

// slice to receive from a remote task
struct recvTOp {
    Laik_Slice slc;
    int sliceNo;
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

// for debug / logging
int laik_getIndexStr(char* s, int dims, Laik_Index* idx);
int laik_getSliceStr(char* s, int dims, Laik_Slice* slc);
int laik_getTransitionStr(char* s, Laik_Transition* t);
int laik_getDataFlowStr(char* s, Laik_DataFlow flow);
int laik_getBorderArrayStr(char* s, Laik_BorderArray* ba);

// initialize the LAIK space module, called from laik_new_instance
void laik_space_init();

#endif // _LAIK_SPACE_INTERNAL_H_

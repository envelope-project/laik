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

typedef struct _Laik_BlockPartitioner Laik_BlockPartitioner;

// internal as depending on communication backend

struct _Laik_Task {
    int rank;
};

// internal to allow extension for non-regular index spaces

struct _Laik_Space {
    char* name; // for debugging
    int id;     // for debugging

    int dims;
    uint64_t size[3]; // at most 3 dimensions
    Laik_Instance* inst;
    Laik_Space* next; // for list of spaces used in instance

    // linked list of partitionings for this space
    Laik_Partitioning* firstSpaceUser;
};

// add/remove partitioning to/from space
void laik_addSpaceUser(Laik_Space* s, Laik_Partitioning* p);
void laik_removeSpaceUser(Laik_Space* s, Laik_Partitioning* p);


struct _Laik_Partitioner {
    Laik_PartitionType type;
    char* name;
    void* data; // partitioner specific data
    laik_run_partitioner_t run;
};

Laik_Partitioner* laik_new_partitioner(Laik_PartitionType t, char* name,
                                       laik_run_partitioner_t f, void* d);

typedef struct _Laik_BlockPartitionerData Laik_BlockPartitionerData;
struct _Laik_BlockPartitionerData {
    int pdim; // dimension to partition, only supports 1d partitionings

    // weighted partitioning (Block) uses callbacks
    Laik_GetIdxWeight_t getIdxW;
    void* idxUserData;
    Laik_GetTaskWeight_t getTaskW;
    void* taskUserData;

    // how many cycles (results in so many slics per task)
    int cycles;
};

typedef struct _Laik_CopyPartitionerData Laik_CopyPartitionerData;
struct _Laik_CopyPartitionerData {
    int fromDim, toDim; // only supports 1d partitionings
    Laik_Partitioning* base;
};


// the output of a partitioner is a Laik_BorderArray

// used in BorderArray to map a slice to a task
typedef struct _Laik_TaskSlice Laik_TaskSlice;
struct _Laik_TaskSlice {
    int task;
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

Laik_BorderArray* allocBorders(Laik_Group* g, Laik_Space* s, int capacity);
// to be used by implementations of partitioners
void appendSlice(Laik_BorderArray* a, int task, Laik_Slice* s);
void clearBorderArray(Laik_BorderArray* a);
void freeBorderArray(Laik_BorderArray* a);

// internal to allow for more irregular partitionings

struct _Laik_Partitioning {
    char* name; // for debugging
    int id;     // for debugging

    Laik_Group* group;
    Laik_Space* space; // space to partition

    Laik_Partitioner* partitioner;

    // partition borders (calculated lazy)
    bool bordersValid;
    Laik_BorderArray* borders;

    // head of list of data containers with this paritioning active
    Laik_Data* firstPartitioningUser;

     // for list of partitionings same space
    Laik_Partitioning* nextSpaceUser;
    // for list of partitionings same space
    Laik_Partitioning* nextGroupUser;
};

// add/remove data container as user to/from partitioning
void laik_addPartitioningUser(Laik_Partitioning* p, Laik_Data* d);
void laik_removePartitioningUser(Laik_Partitioning* p, Laik_Data* d);


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

// LAIK internal
int laik_getIndexStr(char* s, int dims, Laik_Index* idx, bool minus1);
int laik_getTransitionStr(char* s, Laik_Transition* t);
int laik_getDataFlowStr(char* s, Laik_DataFlow flow);

#endif // _LAIK_SPACE_INTERNAL_H_

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

#include "laik-space.h"

#include <stdbool.h>

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
    Laik_Partitioning* first_partitioning;
};

// internal to allow for more irregular partitionings

struct _Laik_Partitioning {
    char* name; // for debugging
    int id;     // for debugging

    Laik_Group* group;
    Laik_PartitionType type;
    Laik_AccessPermission permission;
    Laik_Space* space; // space to partition
    int pdim; // for 2d/3d: dimension to partition

    // weighted partitioning (Stripe) uses callbacks
    Laik_GetIdxWeight_t getIdxW;
    void* idxUserData;
    Laik_GetTaskWeight_t getTaskW;
    void* taskUserData;

    // coupling to another partitioning (potentially other space)
    Laik_Partitioning* base;
    int haloWidth;

    // partitions borders (calculated lazy)
    bool bordersValid;
    Laik_Slice* borders; // slice borders, one slice per participating task

    Laik_Partitioning* next; // for list of partitionings same space
};

#define TRANSSLICES_MAX 10

struct _Laik_Transition {
    int dims;

    // local slices staying local;
    // may need copy when different from/to mappings are used
    int localCount;
    Laik_Slice local[TRANSSLICES_MAX];

    // local slices that should be initialized;
    // the value depends on the reduction type (neutral element)
    int initCount;
    Laik_Slice init[TRANSSLICES_MAX];
    int initRedOp[TRANSSLICES_MAX];

    // slices to send to other task
    int sendCount;
    Laik_Slice send[TRANSSLICES_MAX];
    int sendTo[TRANSSLICES_MAX];

    // slices to receive from other task
    int recvCount;
    Laik_Slice recv[TRANSSLICES_MAX];
    int recvFrom[TRANSSLICES_MAX];

    // slices to reduce
    int redCount;
    Laik_Slice red[TRANSSLICES_MAX];
    int redOp[TRANSSLICES_MAX];
    int redRoot[TRANSSLICES_MAX]; // -1: all
};

// LAIK internal
int laik_getIndexStr(char* s, int dims, Laik_Index* idx, bool minus1);

#endif // _LAIK_SPACE_INTERNAL_H_

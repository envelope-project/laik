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

    // coupling to another partitioning (potentially other space)
    Laik_Partitioning* base;
    int haloWidth;

    // partitions borders (calculated lazy)
    bool bordersValid;
    Laik_Slice* borders; // slice borders, one slice per participating task

    Laik_Partitioning* next; // for list of partitionings same space
};

#define COMMSLICES_MAX 10

struct _Laik_Transition {
    int dims;
    int sendCount, recvCount, redCount;
    Laik_Slice send[COMMSLICES_MAX];
    int sendTo[COMMSLICES_MAX];
    Laik_Slice recv[COMMSLICES_MAX];
    int recvFrom[COMMSLICES_MAX];
    Laik_Slice red[COMMSLICES_MAX];
    int redOp[COMMSLICES_MAX];
    int redRoot[COMMSLICES_MAX]; // -1: all
};

#endif // _LAIK_SPACE_INTERNAL_H_

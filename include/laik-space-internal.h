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

struct _Laik_Group {
    Laik_Instance* inst;
    int gid;
};

// internal to allow extension for non-regular index spaces

struct _Laik_Space {
    int dims, size[3]; // at most 3 dimensions
};

// internal to allow for more irregular partitionings

struct _Laik_Partitioning {
    int count; // number of participating tasks
    Laik_PartitionType type;
    Laik_AccessPermission permission;
    Laik_Space* space; // space to partition

    // coupling to another partitioning (potentially other space)
    Laik_Partitioning* base;
    int coupledDimFrom, coupledDimTo, haloWidth;

    // partitions borders (calculated lazy)
    bool bordersValid;
    Laik_Slice* b; // slice borders, one slice per participating task
};


#endif // _LAIK_SPACE_INTERNAL_H_

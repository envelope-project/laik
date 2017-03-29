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

#ifndef _LAIK_DATA_H_
#define _LAIK_DATA_H_

#include "laik.h"

#include <stdint.h>
#include <stdlib.h>

/*********************************************************************/
/* LAIK Data - Data containers for LAIK index spaces
 *********************************************************************/

// a LAIK container
typedef struct _Laik_Data Laik_Data;

// a serialisation order of a LAIK container
typedef struct _Laik_Layout Laik_Layout;

// container part pinned to local memory space
typedef struct _Laik_Mapping Laik_Mapping;


/*********************************************************************/
/* LAIK API for data containers
 *********************************************************************/

/**
 * Define a LAIK container shared by a LAIK task group.
 * This is a collective operation of all tasks in the group.
 * If no partitioning is set (via laik_setPartition) before
 * use, default to equal-sized owner BLOCK partitioning.
 */
Laik_Data* laik_alloc(Laik_Group* g, Laik_Space* s, int elemsize);
Laik_Data* laik_alloc_1d(Laik_Group* g, int elemsize, uint64_t s1);
Laik_Data* laik_alloc_2d(Laik_Group* g, int elemsize, uint64_t s1, uint64_t s2);

// set a data name, for debug output
void laik_set_data_name(Laik_Data* d, char* n);

// get space used for data
Laik_Space* laik_get_space(Laik_Data*);

// set and enforce a newly created partitioning, and return it
Laik_Partitioning* laik_set_new_partitioning(Laik_Data*,
                                             Laik_PartitionType,
                                             Laik_AccessPermission);

// set and enforce partitioning
void laik_set_partitioning(Laik_Data*, Laik_Partitioning*);


void laik_fill_double(Laik_Data* data, double v);

Laik_Mapping* laik_map(Laik_Data* d, Laik_Layout* l, void** base, uint64_t* count);

void laik_free(Laik_Data*);

//----------------------------------
// Allocator interface
//
// an allocator interface specifies the policy to use for memory resources
// if no allocator is set for a data container, LAIK will use malloc/free
//
// TODO: Split delarations for allocator users vs. allocator implementers

// memory policy to use for a Laik container
typedef enum _Laik_MemoryPolicy {
    LAIK_MP_None = 0,
    LAIK_MP_NewAllocOnRepartition, // reallocate memory at each repartitioning
    LAIK_MP_NotifyOnChange, // notify allocator about needed changes
    LAIK_MP_UsePool,        // no allocate if possible via spare pool resource
} Laik_MemoryPolicy;

// allocator interface
typedef struct _Laik_Allocator Laik_Allocator;
struct _Laik_Allocator {
    Laik_MemoryPolicy policy;

    // called by Laik for allocating resources for a data container
    // usually, Laik_Data parameter can be ignored, but may be used
    // to implement an application-specific policy for a container
    void* (*malloc)(Laik_Data* d, size_t size);
    void (*free)(Laik_Data* d, void* ptr);
    void* (*realloc)(Laik_Data* d, void* ptr, size_t size);

    // notification to allocator that a part of the data is about to be
    // transfered by the communication backend and should be made consistent
    // (used with LAIK_MP_NotifyOnChange)
    void (*unmap)(Laik_Data* d, void* ptr, size_t length);
};

// returns an allocator with default policy LAIK_MP_NewAllocOnRepartition
Laik_Allocator* laik_new_allocator();
void laik_set_allocator(Laik_Data* d, Laik_Allocator* alloc);
Laik_Allocator* laik_get_allocator(Laik_Data* d);

#endif // _LAIK_DATA_H_

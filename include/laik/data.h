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

#ifndef _LAIK_H_
#error "include laik.h instead"
#endif

#include <stdint.h>
#include <stdlib.h>


/*********************************************************************/
/* LAIK Data - Data containers for LAIK index spaces
 *********************************************************************/


//----------------------------------
// Laik data types

typedef struct _Laik_Type Laik_Type;
// predefined
extern Laik_Type *laik_Int32;
extern Laik_Type *laik_Int64;
extern Laik_Type *laik_Float;
extern Laik_Type *laik_Double;

// simple type, no support for reductions
Laik_Type* laik_register_type(char* name, int size);



//----------------------------------
// LAIK data container

typedef struct _Laik_Data Laik_Data;

/**
 * Define a LAIK container shared by a LAIK task group.
 * This is a collective operation of all tasks in the group.
 * If no partitioning is set (via laik_setPartition) before
 * use, default to equal-sized owner BLOCK partitioning.
 */
Laik_Data* laik_alloc(Laik_Group* g, Laik_Space* s, Laik_Type* t);
Laik_Data* laik_alloc_1d(Laik_Group* g, Laik_Type* t, uint64_t s1);
Laik_Data* laik_alloc_2d(Laik_Group* g, Laik_Type* t, uint64_t s1, uint64_t s2);

// set a data name, for debug output
void laik_set_data_name(Laik_Data* d, char* n);

// get space used for data
Laik_Space* laik_get_space(Laik_Data*);

// get task group used for data
Laik_Group* laik_get_group(Laik_Data* d);

// free resources for a data container
void laik_free(Laik_Data*);

// switch from active to another partitioning
void laik_switchto(Laik_Data*, Laik_Partitioning*toP, Laik_DataFlow toFlow);

// get slice number <n> in own partition of data container <d>
// returns 0 if partitioning is not set or slice number <n> is invalid
Laik_Slice* laik_data_slice(Laik_Data* d, int n);

// convenience functions

// switch from active to a newly created partitioning, and return it
Laik_Partitioning* laik_switchto_new(Laik_Data*,
                                     Laik_Partitioner* pr, Laik_DataFlow flow);

void laik_fill_double(Laik_Data* data, double v);



//----------------------------------
// LAIK data mapped to memory space

typedef enum _Laik_LayoutType {
    LAIK_LT_Invalid = 0,
    // not specified, can cope with arbitrary layout
    LAIK_LT_None,
    // possibly multiple slices, each ordered innermost dim 1, then 2, 3
    LAIK_LT_Default,
    // same as Default, but explictily only 1 slice
    LAIK_LT_Default1Slice
} Laik_LayoutType;

// a serialisation order of a LAIK container
typedef struct _Laik_Layout Laik_Layout;

// one slice mapped to local memory space
typedef struct _Laik_Mapping Laik_Mapping;

// list of mappings for all multiple slices
typedef struct _Laik_MappingList Laik_MappingList;

// allocate new layout object with a layout hint, to use in laik_map
Laik_Layout* laik_new_layout(Laik_LayoutType t);

// return the layout used by a mapping
Laik_Layout* laik_map_layout(Laik_Mapping* m);

// return the layout type of a specific layout
Laik_LayoutType laik_layout_type(Laik_Layout* l);

// return the layout type used in a mapping
Laik_LayoutType laik_map_layout_type(Laik_Mapping* m);

// make own partition available for direct access in local memory.
//
// own partition my consist of multiple slices, so <n> is the slice number
// in the partition, starting from 0. Returns 0 for invalid slice numbers.
// The number of slices in own partition is returned by
//   laik_my_slicecount(laik_get_partitioning(<data>));
//
// if layout is 0, it will be choosen by LAIK, and can be requested with
// laik_map_layout(). Otherwise a new layout with a hint can be provided,
// and the layout object directly is written to the actually used layout.
// TODO: API only works for single-slice layouts
Laik_Mapping* laik_map(Laik_Data* d, int n, Laik_Layout* layout);

// similar to laik_map, but force a default mapping
Laik_Mapping* laik_map_def(Laik_Data* d, int n, void** base, uint64_t* count);

// similar to laik_map, but force a default mapping with only 1 slice
Laik_Mapping* laik_map_def1(Laik_Data* d, void** base, uint64_t* count);



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

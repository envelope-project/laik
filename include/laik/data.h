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

#include <stdbool.h>  // for bool
#include <stdint.h>   // for int64_t, uint64_t
#include <stdlib.h>   // for size_t
#include "core.h"     // for Laik_Group, Laik_Instance
#include "space.h"    // for Laik_DataFlow, Laik_AccessPhase, Laik_Partitioning

/*********************************************************************/
/* LAIK Data - Data containers for LAIK index spaces
 *********************************************************************/

// statistics for switching
typedef struct _Laik_SwitchStat Laik_SwitchStat;

//----------------------------------
// Laik data types

typedef struct _Laik_Type Laik_Type;

// predefined
extern Laik_Type *laik_Char;
extern Laik_Type *laik_Int32;
extern Laik_Type *laik_Int64;
extern Laik_Type *laik_UChar;
extern Laik_Type *laik_UInt32;
extern Laik_Type *laik_UInt64;
extern Laik_Type *laik_Float;
extern Laik_Type *laik_Double;

// simple type. To support reductions, need to set callbacks init/reduce
Laik_Type* laik_type_register(char* name, int size);

typedef void (*laik_init_t)(void* base, int count, Laik_ReductionOperation o);
typedef void (*laik_reduce_t)(void* out, void* in1, void* in2,
                              int count, Laik_ReductionOperation o);

// provide an initialization function for this type
void laik_type_set_init(Laik_Type* type, laik_init_t init);

// provide a reduction function for this type
void laik_type_set_reduce(Laik_Type* type, laik_reduce_t reduce);


//----------------------------------
// LAIK data container

typedef struct _Laik_Data Laik_Data;

/**
 * Define a LAIK container shared by a LAIK task group.
 * This is a collective operation of all tasks in the group.
 * If no partitioning is set (via laik_setPartition) before
 * use, default to equal-sized owner BLOCK partitioning.
 */
Laik_Data* laik_new_data(Laik_Space* space, Laik_Type* type);
Laik_Data* laik_new_data_1d(Laik_Instance* i, Laik_Type* t, int64_t s1);
Laik_Data* laik_new_data_2d(Laik_Instance* i, Laik_Type* t, int64_t s1, int64_t s2);

// set a data name, for debug output
void laik_data_set_name(Laik_Data* d, char* n);

// get space used for data
Laik_Space* laik_data_get_space(Laik_Data*);

//  get process group among data currently is distributed
Laik_Group* laik_data_get_group(Laik_Data* d);

// get instance managing data
Laik_Instance* laik_data_get_inst(Laik_Data* d);

// get active partitioning of data container
Laik_Partitioning* laik_data_get_partitioning(Laik_Data* d);

// get active access phase of data container
Laik_AccessPhase* laik_data_get_accessphase(Laik_Data* d);

// free resources for a data container
void laik_free(Laik_Data*);

//
// Reservations for data containers
//
typedef struct _Laik_Reservation Laik_Reservation;

// create a reservation object for <data>
Laik_Reservation* laik_reservation_new(Laik_Data* d);

// register a partitioning for inclusion in a reservation:
// this will include space required for this partitioning on allocation
void laik_reservation_add(Laik_Reservation* r, Laik_Partitioning* p);

// allocate space for all partitionings registered in a reservation
void laik_reservation_alloc(Laik_Reservation* r);

// free the memory space allocated in this reservation
void laik_reservation_free(Laik_Reservation* r);

// make data container aware of reservation:
// when switching to a partitioning, it will use space from reservation
void laik_data_use_reservation(Laik_Data* d, Laik_Reservation* r);



// switch to new partitioning (new flow is derived from previous flow)
void laik_switchto_partitioning(Laik_Data* d,
                                Laik_Partitioning* toP, Laik_DataFlow toFlow);

// switch from active to another access phase
void laik_switchto_phase(Laik_Data* d, Laik_AccessPhase* toAp,
                         Laik_DataFlow toFlow);

// switch to use another data flow, keep access phase/partitioning
void laik_switchto_flow(Laik_Data* d, Laik_DataFlow toFlow);

// migrate data container to use another group
// (only possible if data does not have to be preserved)
void laik_migrate_data(Laik_Data* d, Laik_Group* g);

// get slice number <n> in own partition of data container <d>
// returns 0 if partitioning is not set or slice number <n> is invalid
Laik_TaskSlice* laik_data_slice(Laik_Data* d, int n);

// convenience functions

// switch from active to a newly created access phase using a given
// partitioner algorithm, and return it
Laik_AccessPhase* laik_switchto_new_phase(Laik_Data*, Laik_Group* g,
                                    Laik_Partitioner* pr, Laik_DataFlow flow);

void laik_fill_double(Laik_Data* data, double v);



//----------------------------------
// LAIK data mapped to memory space
//
// global indexes can be signed, as index spaces can include negative ranges
// local indexes are always unsigned, as the index into an address range
//  starting from a base address

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

// for a local index (1d/2d/3d), return offset into memory mapping
int64_t laik_offset(Laik_Index* idx, Laik_Layout* l);

// Make own partition available for direct access in local memory.
// A partition for a task can consist of multiple consecutive ranges
// of memory allocated for the partition. Each range is called a
// mapping. Each mapping may cover multiple slices.
//
// <n> is the mapping ID, going from 0 to number of current mappings -1,
// see laik_my_mapcount(laik_get_partitioning(<data>)).
//
// If <layout> is 0, it will be choosen by LAIK, and can be requested with
// laik_map_layout(). Otherwise a new layout with a hint can be provided,
// and the layout object is updated to reflect the used layout.
// Returns 0 for invalid mapping IDs.
Laik_Mapping* laik_map(Laik_Data* d, int n, Laik_Layout* layout);

// similar to laik_map, but force a default mapping
Laik_Mapping* laik_map_def(Laik_Data* d, int n, void** base, uint64_t* count);

// similar to laik_map, but force a default mapping with only 1 slice
Laik_Mapping* laik_map_def1(Laik_Data* d, void** base, uint64_t* count);

// request default 2d mapping for 1 slice (= rectangle).
// returns the mapping, and values describing mapping in output parameters
//  - valid ranges: y in [0;ysize[, x in [0;xsize[
//  - base[y][x] is at address (base + y * ystride + x)
//    (note: ystride may be larger than xsize)
Laik_Mapping* laik_map_def1_2d(Laik_Data* d, void** base,
                               uint64_t* ysize, uint64_t* ystride,
                               uint64_t* xsize);

// request default 3d mapping for 1 slice (= cuboid).
// returns the mapping, and values describing mapping in output parameters
//  - valid ranges: z in [0;zsize[, y in [0;ysize[, x in [0;xsize[
//  - base[z][y][x] is at address (base + z * zstride + y * ystride + x)
Laik_Mapping* laik_map_def1_3d(Laik_Data* d, void** base,
                               uint64_t* zsize, uint64_t* zstride,
                               uint64_t* ysize, uint64_t* ystride,
                               uint64_t* xsize);

// 1d global to 1d local
// if global index <gidx> is locally mapped, return mapping and set local
//  index <lidx>. Otherwise, return 0
// Note: the local index matches the offset into the local mapping only
//       if the default layout is used
Laik_Mapping* laik_global2local_1d(Laik_Data* d, int64_t gidx, uint64_t* lidx);

// 1d global to 1d local within a given mapping
// if global index <gidx> is locally mapped, return mapping and set local
Laik_Mapping* laik_global2maplocal_1d(Laik_Data* d, int64_t gidx,
                                      int* mapNo, uint64_t* lidx);

// local to global: return global index of local offset in a single local mapping
int64_t laik_local2global_1d(Laik_Data* d, uint64_t off);

// map-local to global
// return global index of local offset in mapping with mapping number <mapNo>
int64_t laik_maplocal2global_1d(Laik_Data* d, int mapNo, uint64_t li);

// return the mapping number of a <map> in the MappingList
int laik_map_get_mapNo(const Laik_Mapping* map);

// 2d global to 2d local
// if global coordinate (gx/gy) is in local mapping, set output parameters
//  (lx/ly) and return mapping, otherwise return false
// to be able to access the mapping, the local offset has to be calculated
// from local coordinates (lx/ly), using memory layout information
Laik_Mapping* laik_global2local_2d(Laik_Data* d, int64_t gx, int64_t gy,
                                   int64_t* lx, int64_t* ly);


// 2d local to 2d global in a single local mapping (thus ...global1).
// if local coordinate (lx/ly) is in local mapping, set output parameters
//  (gx/gy) and return true, otherwise return false
bool laik_local2global1_2d(Laik_Data* d, int64_t lx, int64_t ly,
                           int64_t* gx, int64_t* gy);

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

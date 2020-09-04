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

#ifndef LAIK_DATA_H
#define LAIK_DATA_H

#include <stdbool.h>  // for bool
#include <stdint.h>   // for int64_t, uint64_t
#include <stdlib.h>   // for size_t
#include "core.h"     // for Laik_Group, Laik_Instance
#include "space.h"    // for Laik_DataFlow, Laik_Partitioning
#include "action.h"

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
typedef void (*laik_reduce_t)(void* out, const void* in1, const void* in2,
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

// free reservation and the memory space allocated
void laik_reservation_free(Laik_Reservation* r);

// make data container aware of reservation:
// when switching to a partitioning, it will use space from reservation
void laik_data_use_reservation(Laik_Data* d, Laik_Reservation* r);

// execute a previously calculated transition on a data container
void laik_exec_transition(Laik_Data* d, Laik_Transition* t);

// record steps for a transition on a container into an action sequence,
// optionally provide allocations from reservations which are known
Laik_ActionSeq* laik_calc_actions(Laik_Data* d, Laik_Transition* t,
                                  Laik_Reservation* fromRes,
                                  Laik_Reservation* toRes);

// execute a previously calculated transition on a data container
void laik_exec_actions(Laik_ActionSeq* as);

// switch to new partitioning (new flow is derived from previous flow)
void laik_switchto_partitioning(Laik_Data* d,
                                Laik_Partitioning* toP,
                                Laik_DataFlow flow, Laik_ReductionOperation redOp);

// switch to use another data flow, keep access phase/partitioning
void laik_switchto_flow(Laik_Data* d, Laik_DataFlow flow, Laik_ReductionOperation redOp);

// get slice number <n> in own partition of data container <d>
// returns 0 if partitioning is not set or slice number <n> is invalid
Laik_TaskSlice* laik_data_slice(Laik_Data* d, int n);

// set an initial partitioning for a container.
// memory from a reservation can be used by calling laik_data_use_reservation() before.
// otherwise, memory is not allocated and needs to be provided via laik_set_map_memory().
void laik_set_initial_partitioning(Laik_Data* d, Laik_Partitioning* p);


// convenience functions

// switch to new partitioning calculated with given partitioner algorithm
Laik_Partitioning* laik_switchto_new_partitioning(Laik_Data*, Laik_Group* g,
                                                  Laik_Partitioner* pr,
                                                  Laik_DataFlow flow, Laik_ReductionOperation redOp);


void laik_fill_double(Laik_Data* data, double v);



//----------------------------------
// LAIK data mapped to memory space
//
// global indexes can be signed, as index spaces can include negative ranges
// local indexes are always unsigned, as the index into an address range
//  starting from a base address

// a serialisation order of a LAIK container (for address offsets)
typedef struct _Laik_Layout Laik_Layout;

// one slice mapped to local memory space
typedef struct _Laik_Mapping Laik_Mapping;

// list of mappings for all multiple slices
typedef struct _Laik_MappingList Laik_MappingList;

// return the layout used by a mapping
Laik_Layout* laik_map_layout(Laik_Mapping* m);

// offset for a index inside a slice covered by a layout (1d/2d/3d)
int64_t laik_offset(Laik_Index* idx, Laik_Layout* l);

// copy data in a slice between mappings
void laik_data_copy(Laik_Slice* slc, Laik_Mapping* from, Laik_Mapping* to);


// provide memory resources for a mapping of own partition with ID <n> of container <d>,
// starting at address <base> with <size> bytes. Memory will not be freed by LAIK, and
// it has to cover memory requirements of switches unless reserved memory is provided.
void laik_set_map_memory(Laik_Data* d, int n, void* start, uint64_t size);


// get mapping of own partition into local memory for direct access
//
// A partition for a process can consist of multiple consecutive ranges
// of memory allocated for the partition. Each range is called a
// mapping. Each mapping may cover multiple slices.
//
// <n> is the mapping ID, going from 0 to number of current mappings -1,
// see laik_my_mapcount(laik_get_partitioning(<data>)).
//
// Returns 0 if data not yet mapped or for invalid mapping IDs.
Laik_Mapping* laik_get_map(Laik_Data* d, int n);

// for 1d mapping with ID n, return base pointer and count
Laik_Mapping* laik_get_map_1d(Laik_Data* d, int n, void** base, uint64_t* count);

// for 2d mapping with ID n, describe mapping in output parameters
//  - valid ranges: y in [0;ysize[, x in [0;xsize[
//  - base[y][x] is at address (base + y * ystride + x)
//    (note: ystride may be larger than xsize)
Laik_Mapping* laik_get_map_2d(Laik_Data* d, int n, void** base,
                              uint64_t* ysize, uint64_t* ystride,
                              uint64_t* xsize);

// for 3d mapping with ID n, describe mapping in output parameters
//  - valid ranges: z in [0;zsize[, y in [0;ysize[, x in [0;xsize[
//  - base[z][y][x] is at address (base + z * zstride + y * ystride + x)
Laik_Mapping* laik_get_map_3d(Laik_Data* d, int n, void** base,
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
// otherwise, return 0 and set mapNo to -1
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
// Layout
//
// a layout is a serialisation order of a LAIK container,
// e.g. to define how indexes are layed out in memory

// signatures for layout interface

// return offset into memory mapping for a given index
typedef int64_t (*laik_layout_offset_t)(Laik_Layout*, Laik_Index*);

// copy data in a slice among mappings with same layout type
typedef void (*laik_layout_copy_t)(Laik_Slice* slc,
    Laik_Mapping* from, Laik_Mapping* to);

// set index to index with lowest offset for traversing a given slice,
// return the offset (index 0 maps to offset 0)
typedef int64_t (*laik_layout_first_t)(
    Laik_Layout*, Laik_Slice*, Laik_Index*);

// iteratively traverse a given slice, starting from a given index,
// return the number of consecutive elements possible with a maximum
// of <max> elements. Updates index accordingly
typedef int64_t (*laik_layout_next_t)(
    Laik_Layout*, Laik_Slice*, Laik_Index*, int max);

// pack data of slice in given mapping with this layout into <buf>,
// using at most <size> bytes, starting at index <idx>.
// called iteratively by backends, using <idx> to remember position
// accross multiple calls. <idx> must be set first to index at beginning.
// returns the number of elements written (or 0 if finished)
typedef unsigned int (*laik_layout_pack_t)(
    Laik_Mapping* m, Laik_Slice* s,
    Laik_Index* idx, char* buf, unsigned int size);

// unpack data from <buf> with <size> bytes length into given slice of
// memory space provided by mapping, incrementing index accordingly.
// returns number of elements unpacked.
typedef unsigned int (*laik_layout_unpack_t)(
    Laik_Mapping* m, Laik_Slice* s,
    Laik_Index* idx, char* buf, unsigned int size);

// return string describing the layout (for debug output)
typedef char* (*laik_layout_describe_t)(Laik_Layout*);

void laik_init_layout(Laik_Layout* l, int dims, uint64_t count,
                      laik_layout_pack_t pack,
                      laik_layout_unpack_t unpack,
                      laik_layout_describe_t describe,
                      laik_layout_offset_t offset,
                      laik_layout_copy_t copy,
                      laik_layout_first_t first,
                      laik_layout_next_t next);

// (slow) generic copy just using offset function from layout interface
void laik_layout_copy_gen(Laik_Slice* slc,
                          Laik_Mapping* from, Laik_Mapping* to);


// lexicographical layout covering one 1d, 2d, 3d slice

typedef struct _Laik_Layout_Lex Laik_Layout_Lex;

// create layout object for 1d/2d/3d lexicographical layout
// with innermost dim x, then y, z, fully covering given slice
Laik_Layout* laik_new_layout_lex(Laik_Slice* slc);

// return lex layout if given layout is a lexicographical layout
Laik_Layout_Lex* laik_is_layout_lex(Laik_Layout* l);

// return stride for dimension <d> in lex layout
uint64_t laik_layout_lex_stride(Laik_Layout* l, int d);

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
typedef void* (*Laik_malloc_t)(Laik_Data*, size_t);
typedef void  (*Laik_free_t)(Laik_Data*, void*);
typedef void* (*Laik_realloc_t)(Laik_Data*, void*, size_t);

typedef struct _Laik_Allocator Laik_Allocator;
struct _Laik_Allocator {
    Laik_MemoryPolicy policy;

    // called by Laik for allocating resources for a data container
    // usually, Laik_Data parameter can be ignored, but may be used
    // to implement an application-specific policy for a container
    Laik_malloc_t malloc;
    Laik_free_t free;
    Laik_realloc_t realloc;

    // notification to allocator that a part of the data is about to be
    // transfered by the communication backend and should be made consistent
    // (used with LAIK_MP_NotifyOnChange)
    void (*unmap)(Laik_Data* d, void* ptr, size_t length);
};

Laik_Allocator* laik_new_allocator(Laik_malloc_t, Laik_free_t, Laik_realloc_t);
void laik_set_allocator(Laik_Data* d, Laik_Allocator* alloc);
Laik_Allocator* laik_get_allocator(Laik_Data* d);
// returns an allocator with default policy LAIK_MP_NewAllocOnRepartition
Laik_Allocator* laik_new_allocator_def();

// predefined allocator
extern Laik_Allocator *laik_allocator_def;


#endif // LAIK_DATA_H

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


#ifndef _LAIK_SPACE_H_
#define _LAIK_SPACE_H_

#include <stdbool.h>  // for bool
#include <stdint.h>   // for int64_t, uint64_t
#include "core.h"     // for Laik_Group, Laik_Instance


/*********************************************************************/
/* LAIK Spaces Module - Distributed partitioning of index spaces
 *********************************************************************/


//---------------------------------------------------------------------
// forward decls of structs as typedefs

// a point in an index space
typedef struct _Laik_Index Laik_Index;

// a rectangle-shaped slice from an index space
typedef struct _Laik_Slice Laik_Slice;

// a participating task in the distribution of an index space
typedef struct _Laik_Task Laik_Task;

// an index space (regular and continous, up to 3 dimensions)
typedef struct _Laik_Space Laik_Space;

// a partitioning of an index space with same access behavior
typedef struct _Laik_AccessPhase Laik_AccessPhase;

// set of partitionings to make consistent at the same time
typedef struct _Laik_PartGroup Laik_PartGroup;

// a slice mapped to a task, created by a partitioner
typedef struct _Laik_TaskSlice Laik_TaskSlice;

// calculated partitioning borders, result of a partitioner run
typedef struct _Laik_Partitioning Laik_Partitioning;

// communication requirements when switching partitioning groups
typedef struct _Laik_Transition Laik_Transition;

// a partitioner is an algorithm mapping slices of an index space to tasks
typedef struct _Laik_Partitioner Laik_Partitioner;


//---------------------------------------------------------------------
// enums for space module

/**
 * Laik_DataFlow
 *
 * Specifies the data flow to adhere to when switching from one
 * partitioning to another in a transition.
 */
typedef enum _Laik_DataFlow {
    LAIK_DF_None = 0,

    LAIK_DF_Preserve,  // preserve values in transition to new partitioning
    LAIK_DF_Init,      // initialize with neutral element of reduction operation

} Laik_DataFlow;


/**
 * Laik_ReductionOperation
 *
 * The reduction operation to be executed in a transition
 */
typedef enum _Laik_ReductionOperation {
    LAIK_RO_None = 0,
    LAIK_RO_Sum, LAIK_RO_Prod,
    LAIK_RO_Min, LAIK_RO_Max,
    LAIK_RO_And, LAIK_RO_Or
} Laik_ReductionOperation;

// is this a reduction?
bool laik_is_reduction(Laik_ReductionOperation redOp);


//--------------------------------------------------------------------------
// structs used in the space module: spaces, indexes, slices

/**
 * Laik_Index
 *
 * A point in an index space with at most 3 dimensions.
 *
 * The number of dimensions actually used needs to be maintained externally.
 * The struct layout is part of the public LAIK API.
 */
struct _Laik_Index {
    int64_t i[3]; // at most 3 dimensions
};


// initialize an index struct
void laik_index_init(Laik_Index* i, int64_t i1, int64_t i2, int64_t i3);

// are the indexes equal? Needs number of dimensions as not stored with index
bool laik_index_isEqual(int dims, const Laik_Index* i1, const Laik_Index* i2);


/**
 * Laik_Slice
 *
 * A rectangle-shaped slice from an index space [from;to[.
 *
 * The number of dimensions actually used in from/to members is stored
 * with in the space.
 * The struct layout is part of the public LAIK API.
 */

struct _Laik_Slice {
    Laik_Space* space;
    Laik_Index  from, to;
};

// initialize a slice by providing space and from/to indexes
void laik_slice_init(Laik_Slice* slc, Laik_Space* space,
                     Laik_Index* from, Laik_Index* to);

// initialize a slice by copying parameters from another slice
void laik_slice_init_copy(Laik_Slice* dst, Laik_Slice* src);

// initialize a 1d slice by providing space and from/to values
void laik_slice_init_1d(Laik_Slice* slc, Laik_Space* space,
                        int64_t from, int64_t to);

// initialize a 2d slice by providing space and two from/to values
void laik_slice_init_2d(Laik_Slice* slc, Laik_Space* space,
                        int64_t from1, int64_t to1,
                        int64_t from2, int64_t to2);

// initialize a 3d slice by providing space and three from/to values
void laik_slice_init_3d(Laik_Slice* slc, Laik_Space* space,
                        int64_t from1, int64_t to1,
                        int64_t from2, int64_t to2,
                        int64_t from3, int64_t to3);

// is the given slice empty?
bool laik_slice_isEmpty(Laik_Slice* slc);

// get the intersection of 2 slices; return 0 if intersection is empty
Laik_Slice* laik_slice_intersect(const Laik_Slice* s1, const Laik_Slice* s2);

// expand slice <dst> such that it contains <src>
void laik_slice_expand(Laik_Slice* dst, Laik_Slice* src);

// is slice <slc1> contained in <slc2>?
bool laik_slice_within_slice(const Laik_Slice* slc1, const Laik_Slice* slc2);

// is slice within space borders?
bool laik_slice_within_space(const Laik_Slice* slc, const Laik_Space* sp);

// are the slices equal?
bool laik_slice_isEqual(Laik_Slice* s1, Laik_Slice* s2);

// number of indexes in the slice
uint64_t laik_slice_size(const Laik_Slice* s);


/**
 * Laik_Space
 *
 * A discrete domain consisting of index points which can be related
 * to data and work load. Distribution of data/work load is specified by
 * partitioning a Laik_Space with a paritioner algorithm.
 *
 * The struct is opaque to the LAIK applications.
 */

// create a new index space object (initially invalid)
Laik_Space* laik_new_space(Laik_Instance* inst);

// create a new index space object with an initial size
Laik_Space* laik_new_space_1d(Laik_Instance* i, int64_t s1);
Laik_Space* laik_new_space_2d(Laik_Instance* i, int64_t s1, int64_t s2);
Laik_Space* laik_new_space_3d(Laik_Instance* i,
                              int64_t s1, int64_t s2, int64_t s3);

// free a space with all resources depending on it (e.g. paritionings)
void laik_free_space(Laik_Space* s);

// set a space a name, for debug output
void laik_set_space_name(Laik_Space* s, char* n);

// get the index slice covered by the space
const Laik_Slice* laik_space_asslice(Laik_Space* space);

// number of indexes in the space
uint64_t laik_space_size(const Laik_Space* s);

// get the number of dimensions if this is a regular space
int laik_space_getdimensions(Laik_Space* space);


//--------------------------------------------------------------------------
// LAIK partitionings and partitioner algorithms
//

// create a new partitioning by running an offline partitioner algorithm.
// the partitioner may be derived from another partitioning which is
// forwarded to the partitioner algorithm
Laik_Partitioning* laik_new_partitioning(Laik_Partitioner* pr,
                                         Laik_Group* g, Laik_Space* space,
                                         Laik_Partitioning* otherP);

// free resources allocated for a partitioning object
void laik_free_partitioning(Laik_Partitioning* p);


// give an access phase a name, for debug output
void laik_partitioning_set_name(Laik_Partitioning* p, char* n);


// migrate partitioning to new group without changing borders
// - added tasks get empty partitions
// - removed tasks must have empty partitiongs
void laik_partitioning_migrate(Laik_Partitioning* p, Laik_Group* newg);

// get number of slices for this task
int laik_my_slicecount(Laik_Partitioning* p);

// how many mappings does the partitioning for this process ask for?
int laik_my_mapcount(Laik_Partitioning* p);

// get number of slices within a given mapping for this task
int laik_my_mapslicecount(Laik_Partitioning* p, int mapNo);

// get slice number <n> from the slices for this task
Laik_TaskSlice* laik_my_slice(Laik_Partitioning* p, int n);

// get slice number <n> within mapping <mapNo> from the slices for this task
Laik_TaskSlice* laik_my_mapslice(Laik_Partitioning* p, int mapNo, int n);

// get borders of slice number <n> from the 1d slices for this task
Laik_TaskSlice* laik_my_slice_1d(Laik_Partitioning* p, int n,
                                 int64_t* from, int64_t* to);

// get borders of slice number <n> from the 2d slices for this task
Laik_TaskSlice* laik_my_slice_2d(Laik_Partitioning* p, int n,
                                 int64_t* x1, int64_t* x2,
                                 int64_t* y1, int64_t* y2);

// get borders of slice number <n> from the 3d slices for this task
Laik_TaskSlice* laik_my_slice_3d(Laik_Partitioning* p, int n,
                                 int64_t* x1, int64_t* x2,
                                 int64_t* y1, int64_t* y2,
                                 int64_t* z1, int64_t* z2);



// Partitioner API:
// applications can write their own partitioner algorithms

// Flags for partitioners, to be specified with "laik_new_partitioner()"
typedef enum _Laik_PartitionerFlag {
    LAIK_PF_None = 0,

    // slices with same tag are grouped into same mapping
    // (by default, each slice gets its own mapping, with the tag not used)
    LAIK_PF_GroupByTag = 1,

    // all slices which go into same mapping are packed
    // (by default, there is no packing, eventually with holes,
    //  but making local-to-global index calculation easy).
    LAIK_PF_Compact = 2,

    // the partitioning intentionally does not cover the full space
    // (by default, LAIK checks for full coverage of the index space)
    LAIK_PF_NoFullCoverage = 4,

    // the slices which go into same mapping may have overlapping indexes.
    // This enables a slice merging algorithm
    // (by default, we expect slices not to overlap)
    LAIK_PF_Merge = 8,

    // use an internal data representation optimized for single index slices.
    // this is useful for fine-grained partitioning, requiring indirections
    LAIK_PF_SingleIndex = 16

} Laik_PartitionerFlag;

// Signature for a partitioner algorithm
//
// we are given a new partitioning object without any slices yet (2st par),
// which has to be populated with slices (calling laik_append_slice).
// The border object specifies the group and space to run the partitioner on.
// If 3rd par is not null, it provides partitioning borders the generated
// partitioning may be based on, e.g. for incremental partitioners (modifying
// a previous one) or for derived partitionings (e.g. extending by halos)
typedef void
(*laik_run_partitioner_t)(Laik_Partitioner*,
                          Laik_Partitioning*, Laik_Partitioning*);

// create application-specific partitioner
Laik_Partitioner* laik_new_partitioner(const char* name,
                                       laik_run_partitioner_t run, void* d,
                                       Laik_PartitionerFlag flags);

// functions to be used in own implementation of a partitioner algorithm

// add a slice which should be owned by a given process
//
// the <tag> is a hint for the data layer (if >0):
// - slices with same tag go into same mapping
// - when switching between partitionings, mappings are reused when they are
//   given the same tag >0. For re-use of mappings, if tag 0 is specified,
//   a heuristic is used which checks for highest overlap of indexes.
//   This is also important for reservation semantics
//
// the <data> pointer is an arbitrary value which can be passed from
//  application-specific partitioners to the code processing slices.
//  LAIK provided partitioners set <data> to 0.
Laik_TaskSlice* laik_append_slice(Laik_Partitioning* p, int task, Laik_Slice* s,
                                  int tag, void* data);
// append 1d single-index slice
Laik_TaskSlice* laik_append_index_1d(Laik_Partitioning* p,
                                     int task, int64_t idx);

Laik_Space* laik_partitioning_get_space(Laik_Partitioning* p);
Laik_Group* laik_partitioning_get_group(Laik_Partitioning* p);
int laik_partitioning_slicecount(Laik_Partitioning* p);
Laik_TaskSlice* laik_partitioning_get_tslice(Laik_Partitioning* p, int n);

const Laik_Slice* laik_taskslice_get_slice(Laik_TaskSlice* ts);
int laik_taskslice_get_task(Laik_TaskSlice* ts);
// applications can attach arbitrary values to a TaskSlice, to be
// passed from application-specific partitioners to slice processing
void* laik_taskslice_get_data(Laik_TaskSlice*);
void laik_taskslice_set_data(Laik_TaskSlice*, void* data);
// return the mapping number of this task slice, calculated from tags
// provided by the partitioner
int laik_taskslice_get_mapNo(Laik_TaskSlice*);

// get slice of a task slice
Laik_Slice* laik_tslice_get_slice(Laik_TaskSlice*);


// get a custom data pointer from the partitioner
void* laik_partitioner_data(Laik_Partitioner* partitioner);


// check for assumptions an application may have about a partitioning
bool laik_partitioning_isAll(Laik_Partitioning* p);
int  laik_partitioning_isSingle(Laik_Partitioning* p);
bool laik_partitioning_coversSpace(Laik_Partitioning* p);
bool laik_partitioning_isEqual(Laik_Partitioning* p1, Laik_Partitioning* p2);



// partitioners provided by LAIK

// simple partitioners are available as singletons
extern Laik_Partitioner *laik_Master;
extern Laik_Partitioner *laik_All;

// factory methods to create built-in simple partitioners
Laik_Partitioner* laik_new_all_partitioner();
Laik_Partitioner* laik_new_master_partitioner();
Laik_Partitioner* laik_new_copy_partitioner(int fromDim, int toDim);
Laik_Partitioner* laik_new_cornerhalo_partitioner(int depth);
Laik_Partitioner* laik_new_halo_partitioner(int depth);
Laik_Partitioner* laik_new_bisection_partitioner();
Laik_Partitioner* laik_new_grid_partitioner(int xblocks, int yblocks,
                                            int zblocks);

// block partitioner
typedef double (*Laik_GetIdxWeight_t)(Laik_Index*, const void* userData);
typedef double (*Laik_GetTaskWeight_t)(int rank, const void* userData);
Laik_Partitioner* laik_new_block_partitioner(int pdim, int cycles,
                                             Laik_GetIdxWeight_t ifunc,
                                             Laik_GetTaskWeight_t tfunc,
                                             const void* userData);

// block partitioner for 1d space without weighting
Laik_Partitioner* laik_new_block_partitioner1();
// block partitioner for 1d space with index-wise weighting
Laik_Partitioner* laik_new_block_partitioner_iw1(Laik_GetIdxWeight_t f,
                                                 const void* userData);
// block partitioner for 1d space with task-wise weighting
Laik_Partitioner* laik_new_block_partitioner_tw1(Laik_GetTaskWeight_t f,
                                                 const void* userData);

// set index-wise weight getter, used when calculating BLOCK partitioning.
// as getter is called in every LAIK task, weights have to be known globally
// (useful if workload per index is known)
void laik_set_index_weight(Laik_Partitioner* p, Laik_GetIdxWeight_t f,
                           const void* userData);

// set task-wise weight getter, used when calculating BLOCK partitioning.
// as getter is called in every LAIK task, weights have to be known globally
// (useful if relative performance per task is known)
void laik_set_task_weight(Laik_Partitioner* pr, Laik_GetTaskWeight_t f,
                          const void* userData);

// for block partitionings, we can specify how often we go around in cycles
// to distribute chunks to tasks. Default is 1.
void laik_set_cycle_count(Laik_Partitioner* p, int cycles);

// Reassign: incremental partitioner
// redistribute indexes from tasks to be removed
// this partitioner can make use of application-specified index weights
Laik_Partitioner*
laik_new_reassign_partitioner(Laik_Group* newg,
                              Laik_GetIdxWeight_t getIdxW,
                              const void* userData);


// get local index from global one. return false if not local
bool laik_index_global2local(Laik_Partitioning*,
                             Laik_Index* global, Laik_Index* local);


// Calculate communication required for transitioning between partitionings
Laik_Transition*
laik_calc_transition(Laik_Space* space,
                     Laik_Partitioning* fromP, Laik_Partitioning* toP,
                     Laik_DataFlow flow, Laik_ReductionOperation redOp);


#endif // _LAIK_SPACE_H_

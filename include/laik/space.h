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


#ifndef LAIK_SPACE_H
#define LAIK_SPACE_H

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

// a rectangle-shaped range from an index space
typedef struct _Laik_Range Laik_Range;

// a participating task in the distribution of an index space
typedef struct _Laik_Task Laik_Task;

// an index space (regular and continous, up to 3 dimensions)
typedef struct _Laik_Space Laik_Space;

// a partitioning of an index space with same access behavior
typedef struct _Laik_AccessPhase Laik_AccessPhase;

// set of partitionings to make consistent at the same time
typedef struct _Laik_PartGroup Laik_PartGroup;

// a range assigned to a task, created by a partitioner
typedef struct _Laik_TaskRange Laik_TaskRange;

// calculated partitioning borders, result of a partitioner run
typedef struct _Laik_Partitioning Laik_Partitioning;

// communication requirements when switching partitioning groups
typedef struct _Laik_Transition Laik_Transition;

// a partitioner is an algorithm assigning ranges of an index space to tasks
typedef struct _Laik_Partitioner Laik_Partitioner;

// input parameters for a partitioner run
typedef struct _Laik_PartitionerParams Laik_PartitionerParams;

// an ordered sequence of ranges assigned to tasks
typedef struct _Laik_RangeList Laik_RangeList;

// parameters for filtering ranges during a partitioner run
typedef struct _Laik_RangeFilter Laik_RangeFilter;

// context during a partitioner run
typedef struct _Laik_RangeReceiver Laik_RangeReceiver;


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
    LAIK_RO_None = 0, // Invalid reduction
    LAIK_RO_Sum, LAIK_RO_Prod,
    LAIK_RO_Min, LAIK_RO_Max,
    LAIK_RO_And, LAIK_RO_Or,
    LAIK_RO_Any,    // can copy any of the inputs
    LAIK_RO_Single, // only single input (= copy) allowed, error otherwise
    LAIK_RO_Custom = 100 // user-provided reductions on user types
} Laik_ReductionOperation;

// is this a reduction?
bool laik_is_reduction(Laik_ReductionOperation redOp);


//--------------------------------------------------------------------------
// structs used in the space module: spaces, indexes, ranges

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
 * Laik_Range
 *
 * A range from an index space [from;to[.
 *
 * The number of dimensions actually used in from/to members is stored
 * in the referenced space object.
 * This struct is public.
 */

struct _Laik_Range {
    Laik_Space* space;
    Laik_Index  from, to;
};

// initialize a range by providing space and from/to indexes
void laik_range_init(Laik_Range* range, Laik_Space* space,
                     Laik_Index* from, Laik_Index* to);

// initialize a range by copying parameters from another range
void laik_range_init_copy(Laik_Range* dst, Laik_Range* src);

// initialize a 1d range by providing space and from/to values
void laik_range_init_1d(Laik_Range* range, Laik_Space* space,
                        int64_t from, int64_t to);

// initialize a 2d range by providing space and two from/to values
void laik_range_init_2d(Laik_Range* range, Laik_Space* space,
                        int64_t from1, int64_t to1,
                        int64_t from2, int64_t to2);

// initialize a 3d range by providing space and three from/to values
void laik_range_init_3d(Laik_Range* range, Laik_Space* space,
                        int64_t from1, int64_t to1,
                        int64_t from2, int64_t to2,
                        int64_t from3, int64_t to3);

// is the given range empty?
bool laik_range_isEmpty(Laik_Range*);

// get the intersection of two ranges; return 0 if intersection is empty
Laik_Range* laik_range_intersect(const Laik_Range* r1, const Laik_Range* r2);

// expand range <dst> such that it contains <src>
void laik_range_expand(Laik_Range* dst, Laik_Range* src);

// is range <r1> contained in <r2>?
bool laik_range_within_range(const Laik_Range* r1, const Laik_Range* r2);

// is range within space borders?
bool laik_range_within_space(const Laik_Range* range, const Laik_Space* sp);

// are the ranges equal?
bool laik_range_isEqual(Laik_Range* r1, Laik_Range* r2);

// number of indexes in the range
uint64_t laik_range_size(const Laik_Range* r);


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

void laik_change_space_1d(Laik_Space* s, int64_t from1, int64_t to1);
void laik_change_space_2d(Laik_Space* s,
                          int64_t from1, int64_t to1, int64_t from2, int64_t to2);
void laik_change_space_3d(Laik_Space* s, int64_t from1, int64_t to1,
                          int64_t from2, int64_t to2, int64_t from3, int64_t to3);

// free a space with all resources depending on it (e.g. paritionings)
void laik_free_space(Laik_Space* s);

// set a space a name, for debug output
void laik_set_space_name(Laik_Space* s, char* n);

// get the index range covered by the space
const Laik_Range* laik_space_asrange(Laik_Space* space);

// number of indexes in the space
uint64_t laik_space_size(const Laik_Space* s);

// get the number of dimensions if this is a regular space
int laik_space_getdimensions(Laik_Space* space);


/**
 * Laik_RangeList
 *
 * A sequence of ranges assigned to task ids.
 * After adding ranges, the list must be freezed before accessing ranges.
 */

// create a list to hold ranges for a given space with max task id
Laik_RangeList* laik_rangelist_new(Laik_Space* space, unsigned int max_tid);
void laik_rangelist_free(Laik_RangeList* list);

// add a range with tag and arbitrary data to a range list
void laik_rangelist_append(Laik_RangeList* list, int tid, const Laik_Range *range,
                           int tag, void* data);
// add a range with a single 1d index to a range list (space optimized)
void laik_rangelist_append_single1d(Laik_RangeList* list, int tid, int64_t idx);
// freeze range list
void laik_rangelist_freeze(Laik_RangeList* list, bool doMerge);
// translate task ids using <idmap> array
void laik_rangelist_migrate(Laik_RangeList* list, int* idmap, unsigned int new_count);

// does this list cover the full space with one range for each process?
bool laik_rangelist_isAll(Laik_RangeList* list);
// does this list cover the full space with one range in exactly one task?
int laik_rangelist_isSingle(Laik_RangeList* list);
// are the ranges of two range lists equal?
bool laik_rangelist_isEqual(Laik_RangeList* r1, Laik_RangeList* r2);
// do the ranges of this partitioning cover the full space?
bool laik_rangelist_coversSpace(Laik_RangeList* list);
// get number of ranges
int laik_rangelist_rangecount(Laik_RangeList* list);
int laik_rangelist_tidrangecount(Laik_RangeList* list, int tid);
int laik_rangelist_tidmapcount(Laik_RangeList* list, int tid);
unsigned int laik_rangelist_tidmaprangecount(Laik_RangeList* list, int tid, int mapNo);
// get a given task range from a range list
Laik_TaskRange* laik_rangelist_taskrange(Laik_RangeList* list, int n);
Laik_TaskRange* laik_rangelist_tidrange(Laik_RangeList* list, int tid, int n);
Laik_TaskRange* laik_rangelist_tidmaprange(Laik_RangeList* list, int tid, int mapNo, int n);


/**
 * Laik_RangeFilter
 *
 * Allows to filter ranges in a partitioner run, storing only a subset
 * The filter is passed in laik_run_partitioner().
 */
Laik_RangeFilter* laik_rangefilter_new(void);
void laik_rangefilter_free(Laik_RangeFilter*);
// set filter to only keep ranges for own process when adding ranges
void laik_rangefilter_set_myfilter(Laik_RangeFilter* sf, Laik_Group* g);
// add filter to only keep ranges intersecting with ranges in <list>
void laik_rangefilter_add_idxfilter(Laik_RangeFilter* sf, Laik_RangeList* list, int tid);


// Partitioner API:
// applications can write their own partitioner algorithms

// Flags for partitioners, to be specified with "laik_new_partitioner()"
typedef enum _Laik_PartitionerFlag {
    LAIK_PF_None = 0,

    // ranges with same tag are grouped into same mapping
    // (by default, each range gets its own mapping, with the tag not used)
    LAIK_PF_GroupByTag = 1,

    // all ranges which go into same mapping are packed
    // (by default, there is no packing, eventually with holes,
    //  but making local-to-global index calculation easy).
    LAIK_PF_Compact = 2,

    // the partitioning intentionally does not cover the full space
    // (by default, LAIK checks for full coverage of the index space)
    LAIK_PF_NoFullCoverage = 4,

    // the ranges which go into same mapping may have overlapping indexes.
    // This enables a range merging algorithm
    // (by default, we expect ranges not to overlap)
    LAIK_PF_Merge = 8,

    // use an internal data representation optimized for single index ranges.
    // this is useful for fine-grained partitioning, requiring indirections
    LAIK_PF_SingleIndex = 16

} Laik_PartitionerFlag;

// input parameters for a specific run of a partitioner algorithm
struct _Laik_PartitionerParams {
    Laik_Space* space;
    Laik_Group* group;
    Laik_Partitioner* partitioner;
    Laik_Partitioning* other;
};

// Signature for a partitioner algorithm
//
// We are given a new partitioning object without any ranges yet (2st par),
// which has to be populated with ranges (calling laik_append_range). The
// partitioning object specifies the group and space to run the partitioner on.
// If 3rd par is not null, it provides partitioning borders the generated
// partitioning may be based on, e.g. for incremental partitioners (modifying
// a previous one) or for derived partitionings (e.g. extending by halos)
typedef void
    (*laik_run_partitioner_t)(Laik_RangeReceiver*, Laik_PartitionerParams*);


// create application-specific partitioner
Laik_Partitioner* laik_new_partitioner(const char* name,
                                       laik_run_partitioner_t run, void* d,
                                       Laik_PartitionerFlag flags);

// run a partitioner with given input parameters and filter
Laik_RangeList* laik_run_partitioner(Laik_PartitionerParams* params,
                                      Laik_RangeFilter* filter);

// functions to be used in own implementation of a partitioner algorithm

// add a range which should be owned by a given process
//
// the <tag> is a hint for the data layer (if >0):
// - ranges with same tag go into same mapping
// - when switching between partitionings, mappings are reused when they are
//   given the same tag >0. For re-use of mappings, if tag 0 is specified,
//   a heuristic is used which checks for highest overlap of indexes.
//   This is also important for reservation semantics
//
// the <data> pointer is an arbitrary value which can be passed from
//  application-specific partitioners to the code processing ranges.
//  LAIK provided partitioners set <data> to 0.
void laik_append_range(Laik_RangeReceiver* r, int task, const Laik_Range* s,
                       int tag, void* data);
// append 1d single-index range
void laik_append_index_1d(Laik_RangeReceiver* r, int task, int64_t idx);


/**
 * Laik_Partitioning
 */

// create a new invalid partitioning
Laik_Partitioning* laik_new_empty_partitioning(Laik_Group* g, Laik_Space* s,
                                               Laik_Partitioner *pr, Laik_Partitioning *other);

// create a new empty, invalid partitioning using same parameters as in given one
Laik_Partitioning* laik_clone_empty_partitioning(Laik_Partitioning* p);

// ranges from a partitioner run without filter
Laik_RangeList* laik_partitioning_allranges(Laik_Partitioning*);
// ranges from a partitioner run keeping only ranges of this process
Laik_RangeList* laik_partitioning_myranges(Laik_Partitioning*);
// ranges from run intersecting with own ranges of <p1> and <p2>
Laik_RangeList* laik_partitioning_interranges(Laik_Partitioning* p1,
                                              Laik_Partitioning* p2);

// run the partitioner specified for the partitioning, keeping all ranges
void laik_partitioning_store_allranges(Laik_Partitioning* p);
// run the partitioner specified for the partitioning, keeping only ranges of own process
void laik_partitioning_store_myranges(Laik_Partitioning* p);
// run the partitioner specified for the partitioning, keeping intersecting ranges
void laik_partitioning_store_intersectranges(Laik_Partitioning* p, Laik_Partitioning* p2);


// create a new partitioning by running an offline partitioner algorithm.
// the partitioner may be derived from another partitioning which is
// forwarded to the partitioner algorithm
Laik_Partitioning* laik_new_partitioning(Laik_Partitioner* pr,
                                         Laik_Group* g, Laik_Space* space,
                                         Laik_Partitioning* otherP);

// new partitioning taking ranges from another, migrating to new group
Laik_Partitioning* laik_new_migrated_partitioning(Laik_Partitioning* other,
                                                  Laik_Group* newg);

// free resources allocated for a partitioning object
void laik_free_partitioning(Laik_Partitioning* p);


// give an access phase a name, for debug output
void laik_partitioning_set_name(Laik_Partitioning* p, char* n);

// migrate partitioning to new group without changing borders
// - added tasks get empty partitions
// - removed tasks must have empty partitiongs
void laik_partitioning_migrate(Laik_Partitioning* p, Laik_Group* newg);

// get number of ranges for own process
int laik_my_rangecount(Laik_Partitioning* p);

// how many mappings does the partitioning for this process ask for?
int laik_my_mapcount(Laik_Partitioning* p);

// get number of ranges within a given mapping for this task
int laik_my_maprangecount(Laik_Partitioning* p, int mapNo);

// get range number <n> from ranges for own process
Laik_TaskRange* laik_my_range(Laik_Partitioning* p, int n);

// get range number <n> within mapping <mapNo> from the ranges for own process
Laik_TaskRange* laik_my_maprange(Laik_Partitioning* p, int mapNo, int n);

// get borders of range number <n> from the 1d ranges for own process
Laik_TaskRange* laik_my_range_1d(Laik_Partitioning* p, int n,
                                 int64_t* from, int64_t* to);

// get borders of range number <n> from the 2d ranges for own process
Laik_TaskRange* laik_my_range_2d(Laik_Partitioning* p, int n,
                                 int64_t* x1, int64_t* x2,
                                 int64_t* y1, int64_t* y2);

// get borders of range number <n> from the 3d ranges for own process
Laik_TaskRange* laik_my_range_3d(Laik_Partitioning* p, int n,
                                 int64_t* x1, int64_t* x2,
                                 int64_t* y1, int64_t* y2,
                                 int64_t* z1, int64_t* z2);



Laik_Space* laik_partitioning_get_space(Laik_Partitioning* p);
Laik_Group* laik_partitioning_get_group(Laik_Partitioning* p);
int laik_partitioning_rangecount(Laik_Partitioning* p);
Laik_TaskRange* laik_partitioning_get_taskrange(Laik_Partitioning* p, int n);


// get range of a task range
const Laik_Range* laik_taskrange_get_range(Laik_TaskRange* trange);
int laik_taskrange_get_task(Laik_TaskRange* trange);
// applications can attach arbitrary values to a TaskRange, to be
// passed from application-specific partitioners to range processing
void* laik_taskrange_get_data(Laik_TaskRange*trange);
void laik_taskrange_set_data(Laik_TaskRange*trange, void* data);
// return the mapping number of this task range, calculated from tags
// provided by the partitioner
int laik_taskrange_get_mapNo(Laik_TaskRange*trange);
int laik_taskrange_get_tag(Laik_TaskRange* trange);

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
Laik_Partitioner* laik_new_all_partitioner(void);
Laik_Partitioner* laik_new_master_partitioner(void);
Laik_Partitioner* laik_new_copy_partitioner(int fromDim, int toDim);
Laik_Partitioner* laik_new_cornerhalo_partitioner(int depth);
Laik_Partitioner* laik_new_halo_partitioner(int depth);
Laik_Partitioner* laik_new_bisection_partitioner(void);
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
Laik_Partitioner* laik_new_block_partitioner1(void);
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

// free a transition
void laik_free_transition(Laik_Transition* t);


#endif // LAIK_SPACE_H

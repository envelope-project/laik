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

#ifndef _LAIK_SPACE_H_
#define _LAIK_SPACE_H_

#ifndef _LAIK_H_
#error "include laik.h instead"
#endif

#include <stdint.h>
#include <stdbool.h>

/*********************************************************************/
/* LAIK Spaces - Distributed partitioning of index spaces
 *********************************************************************/

// A Partitioner implements a specific algorithm to partition
// an index space. There are predefined partitioners, but applications
// may define their own.

typedef struct _Laik_Partitioner Laik_Partitioner;


/**
 * Set of flags describing how the application accesses its
 * partition of a LAIK data container in a phase, and thus how
 * data needs to be preserved from previous and to next phase.
 *
 * This is used to decide which data needs to be transfered or
 * copied, and whether memory resources can be shared among
 * LAIK tasks.
 *
 * Flags:
 * - CopyIn:  data needs to be preserved from previous phase
 * - CopyOut: data needs to be preserved for next phase
 * - Init:    values need to be initialized for reduction
 * - ReduceOut: values will added for next phase
 *
 * - Sharable: it is safe for LAIK tasks to use shared memory
 *
 * Consistency rules:
 * - CopyIn only possible if previous phase is CopyOut or
 *   ReduceOut
 **/
typedef enum _Laik_DataFlow {
    LAIK_DF_None       = 0,

    LAIK_DF_CopyIn     = 1,     // preserve values of previous phase
    LAIK_DF_CopyOut    = 2,     // propagate values to next phase
    LAIK_DF_Init       = 4,     // Initialize, needs reduction operation
    LAIK_DF_ReduceOut  = 8,     // output aggregated, needs reduction operation

    LAIK_DF_Sharable   = 16,    // tasks can share memory

    LAIK_DF_Sum        = 1<<16  // sum reduction (for Init/ReduceOut)
} Laik_DataFlow;

// reduction operation
typedef enum _Laik_ReductionOperation {
    LAIK_RO_None = 0,
    LAIK_RO_Sum
} Laik_ReductionOperation;

// a point in an index space
typedef struct _Laik_Index Laik_Index;
struct _Laik_Index {
    uint64_t i[3]; // at most 3 dimensions
};

// a rectangle-shaped slice from an index space [from;to[
typedef struct _Laik_Slice Laik_Slice;
struct _Laik_Slice {
  Laik_Index from, to;
};

// a participating task in the distribution of an index space
typedef struct _Laik_Task Laik_Task;

// an index space (regular and continous, up to 3 dimensions)
typedef struct _Laik_Space Laik_Space;

// a partitioning of an index space with same access behavior
typedef struct _Laik_Partitioning Laik_Partitioning;

// set of partitionings to make consistent at the same time
typedef struct _Laik_PartGroup Laik_PartGroup;

// a slice mapped to a task, created by a partitioner
typedef struct _Laik_TaskSlice Laik_TaskSlice;

// calculated partitioning borders, final result of a partitioner run
typedef struct _Laik_BorderArray Laik_BorderArray;

// communication requirements when switching partitioning groups
typedef struct _Laik_Transition Laik_Transition;



/*********************************************************************/
/* LAIK API for distributed index spaces
 *********************************************************************/

// is this a reduction?
bool laik_is_reduction(Laik_DataFlow flow);
// return the reduction operation from data flow behavior
Laik_ReductionOperation laik_get_reduction(Laik_DataFlow flow);
// do we need to copy values in?
bool laik_do_copyin(Laik_DataFlow flow);
// do we need to copy values out?
bool laik_do_copyout(Laik_DataFlow flow);
// Do we need to init values?
bool laik_do_init(Laik_DataFlow flow);


//
// LAIK spaces, indexes, slices
//

// create a new index space object (initially invalid)
Laik_Space* laik_new_space(Laik_Instance* inst);

// create a new index space object with an initial size
Laik_Space* laik_new_space_1d(Laik_Instance* i, uint64_t s1);
Laik_Space* laik_new_space_2d(Laik_Instance* i,
                              uint64_t s1, uint64_t s2);
Laik_Space* laik_new_space_3d(Laik_Instance* i,
                              uint64_t s1, uint64_t s2, uint64_t s3);

// free a space with all resources depending on it (e.g. paritionings)
void laik_free_space(Laik_Space* s);

// number of indexes in the space
uint64_t laik_space_size(Laik_Space* s);

// set a space a name, for debug output
void laik_set_space_name(Laik_Space* s, char* n);

// change the size of an index space, eventually triggering a repartitiong
void laik_change_space_1d(Laik_Space* s, uint64_t from1, uint64_t to1);

// are the indexes equal?
bool laik_index_isEqual(int dims, Laik_Index* i1, Laik_Index* i2);

// is the given slice empty?
bool laik_slice_isEmpty(int dims, Laik_Slice* slc);

// get the intersection of 2 slices; return 0 if intersection is empty
Laik_Slice* laik_slice_intersect(int dims, const Laik_Slice* s1, const Laik_Slice* s2);

// expand slice <dst> such that it contains <src>
void laik_slice_expand(int dims, Laik_Slice* dst, Laik_Slice* src);

// is slice <slc1> contained in <slc2>?
bool laik_slice_within_slice(int dims, Laik_Slice* slc1, Laik_Slice* slc2);

// is slice within space borders?
bool laik_slice_within_space(Laik_Slice* slc, Laik_Space* sp);

// are the slices equal?
bool laik_slice_isEqual(int dims, Laik_Slice* s1, Laik_Slice* s2);

// number of indexes in the slice
uint64_t laik_slice_size(int dims, Laik_Slice* s);

// get the index slice covered by the space
const Laik_Slice* laik_space_getslice(Laik_Space* space);

// get the number of dimensions if this is a regular space
int laik_space_getdimensions(Laik_Space* space);

//
// LAIK partitioners
//

// Flags for partitioners

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

} Laik_PartitionerFlag;


// Signature for a partitioner algorithm
//
// we are given a new border object without any slices yet (2st par),
// which has to be populated with slices (calling laik_append_slice).
// The border object specifies the group and space to run the partitioner on.
// If 3rd par is not null, it provides old borders to allow incremental
// partitioner algorithms
typedef void
(*laik_run_partitioner_t)(Laik_Partitioner*,
                          Laik_BorderArray*, Laik_BorderArray*);

// create application-specific partitioner
Laik_Partitioner* laik_new_partitioner(const char* name,
                                       laik_run_partitioner_t run, void* d,
                                       Laik_PartitionerFlag flags);

// to be used by implementations of partitioners

// the <tag> is a hint for the data layer: if >0, slices with same tag go
//  into same mapping.
// the <data> pointer is an arbitrary value which can be passed from
//  application-specific partitioners to the code processing slices.
//  LAIK provided partitioners set <data> to 0.
Laik_TaskSlice* laik_append_slice(Laik_BorderArray* a, int task, Laik_Slice* s,
                                  int tag, void* data);

Laik_Space* laik_borderarray_getspace(Laik_BorderArray* ba);
Laik_Group* laik_borderarray_getgroup(Laik_BorderArray* ba);
int laik_borderarray_getcount(Laik_BorderArray* ba);
Laik_TaskSlice* laik_borderarray_get_tslice(Laik_BorderArray* ba, int n);
const Laik_Slice* laik_taskslice_getslice(Laik_TaskSlice* ts);
int laik_taskslice_gettask(Laik_TaskSlice* ts);

// get a custom data pointer from the partitioner
void* laik_partitioner_data(Laik_Partitioner* partitioner);

// Partitioners provided by LAIK

// simple partitioners are available as singletons
extern Laik_Partitioner *laik_Master;
extern Laik_Partitioner *laik_All;

// create a built-in partitioner
Laik_Partitioner* laik_new_all_partitioner();
Laik_Partitioner* laik_new_master_partitioner();
Laik_Partitioner* laik_new_copy_partitioner(int fromDim, int toDim);
Laik_Partitioner* laik_new_cornerhalo_partitioner(int depth);
Laik_Partitioner* laik_new_halo_partitioner(int depth);
Laik_Partitioner* laik_new_bisection_partitioner();


// Block partitioner

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


//
// LAIK partitionings
//

// Create a new partitioning object for a group on a space, using a
// given partitioning algorithm and optionally a base partitioning
// to couple the new partitioining to.
//
// The partitioner will be called to calculate borders
// - if borders are needed but not set yet
//   (on laik_my_slice or when a data container is switched to it)
// - for repartitioning when partitioning is migrated to another group
//   (done e.g. when group is shrinked/enlarged from external)
// - whenever a given base partitioning changes
Laik_Partitioning*
laik_new_partitioning(Laik_Group* group, Laik_Space* space,
                      Laik_Partitioner* pr, Laik_Partitioning* base);

// return partitioner set for a partitioning
Laik_Partitioner* laik_get_partitioner(Laik_Partitioning* p);

// set the partitioner to use (can be custom, application-specific)
void laik_set_partitioner(Laik_Partitioning* p, Laik_Partitioner* pr);

// get space used in partitioning
Laik_Space* laik_get_pspace(Laik_Partitioning* p);

// get task group used in partitioning
Laik_Group* laik_get_pgroup(Laik_Partitioning* p);

// free a partitioning with related resources
void laik_free_partitioning(Laik_Partitioning* p);

// get number of slices of this task
int laik_my_slicecount(Laik_Partitioning* p);

// get number of mappings of this task
int laik_my_mapcount(Laik_Partitioning* p);

// get slice number <n> from the slices of this task
Laik_TaskSlice* laik_my_slice(Laik_Partitioning* p, int n);

// get from/to values for 1d slice with number <n> assigned to this task
// if there is no slice, return 0 and set range to [0;0[ (ie. empty)
Laik_TaskSlice* laik_my_slice_1d(Laik_Partitioning* p, int n,
                                 uint64_t* from, uint64_t* to);

// get boundaries [x1;x2[ x [y1;y2[ for 2d slice <n> of this task
// if there is no slice, return 0 and set ranges to [0;0[ (ie. empty)
Laik_TaskSlice* laik_my_slice_2d(Laik_Partitioning* p, int n,
                                 uint64_t* x1, uint64_t* x2,
                                 uint64_t* y1, uint64_t* y2);

// get boundaries [x1;x2[ x [y1;y2[ x [z1;z2[ for 3d slice <n> of this task
// if there is no slice, return 0 and set ranges to [0;0[ (ie. empty)
Laik_TaskSlice* laik_my_slice_3d(Laik_Partitioning* p, int n,
                                 uint64_t* x1, uint64_t* x2,
                                 uint64_t* y1, uint64_t* y2,
                                 uint64_t* z1, uint64_t* z2);

// applications can attach arbitrary values to a TaskSlice, to be
// passed from application-specific partitioners to slice processing
void* laik_get_slice_data(Laik_TaskSlice*);
void laik_set_slice_data(Laik_TaskSlice*, void* data);

// give a partitioning a name, for debug output
void laik_set_partitioning_name(Laik_Partitioning* p, char* n);

// run a partitioner, returning newly calculated borders
// the partitioner may use old borders from <oldBA>
Laik_BorderArray* laik_run_partitioner(Laik_Partitioner* pr,
                                       Laik_Group* g, Laik_Space* space,
                                       Laik_BorderArray* otherBA);

// set new partitioning borders
void laik_set_borders(Laik_Partitioning* p, Laik_BorderArray* ba);

// return currently set borders in partitioning
Laik_BorderArray* laik_get_borders(Laik_Partitioning* p);

// calculate partition borders
void laik_calc_partitioning(Laik_Partitioning* p);

// append a partitioning to a partioning group whose consistency should
// be enforced at the same point in time
void laik_append_partitioning(Laik_PartGroup* g, Laik_Partitioning* p);

// Calculate communication required for transitioning between
// calculated borders of partitionings
Laik_Transition*
laik_calc_transition(Laik_Group* group, Laik_Space* space,
                     Laik_BorderArray* fromBA, Laik_DataFlow fromFlow,
                     Laik_BorderArray* toBA, Laik_DataFlow toFlow);

// Calculate communication for transitioning between partitioning groups
Laik_Transition* laik_calc_transitionG(Laik_PartGroup* from,
                                       Laik_PartGroup* to);

// enforce consistency for the partitioning group, depending on previous
void laik_enforce_consistency(Laik_Instance* i, Laik_PartGroup* g);


// couple different LAIK instances via spaces:
// one partition of calling task in outer space is mapped to inner space
void laik_couple_nested(Laik_Space* outer, Laik_Space* inner);

// migrate borders to new group without changing borders
// - added tasks get empty partitions
// - removed tasks must have empty partitiongs
void laik_migrate_borders(Laik_BorderArray* ba, Laik_Group* newg);

// migrate a partitioning defined on one task group to another group
// if borders are set, this only is successful if partitions of
// tasks which are not in the new group are empty.
// return true if migration was successful.
bool laik_migrate_partitioning(Laik_Partitioning* p, Laik_Group* newg);

// migrate a partitioning defined on one task group to another group
// for the required repartitioning, use the default partitioner
void laik_migrate_and_repartition(Laik_Partitioning* p, Laik_Group* newg,
                                  Laik_Partitioner* pr);


//------------------------------------------
// automatic repartitioning

typedef enum _Laik_RepartitionHint {
    LAIK_RH_None = 0,
    LAIK_RH_Update      = 1,  // update by re-checking parameter
    LAIK_RH_External    = 2,  // check external sources
    LAIK_RH_Incremental = 4   // try to keep changes small
} Laik_RepartitionHint;

// allow LAIK to change a partitioning based on external means
// returns true if partitioning was changed
bool allowRepartitioning(Laik_Partitioning* p);

#endif // _LAIK_SPACE_H_

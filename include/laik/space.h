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
/* LAIK Spaces - Distributed partitioning of index spaces
 *********************************************************************/

// A Partitioner implements a specific algorithm to partition
// an index space. There are predefined partitioners, but applications
// may define their own.

typedef struct _Laik_Partitioner Laik_Partitioner;


/**
 * Data flows to adhere to when switching from one partitioning
 * to another (ie. in a transition).
 *
 * Consistency rules:
 * - CopyIn only possible if previous phase is CopyOut or
 *   ReduceOut
 **/
typedef enum _Laik_DataFlow {
    LAIK_DF_None = 0,

    LAIK_DF_CopyIn,          // preserve values of previous phase
    LAIK_DF_CopyOut,         // propagate values to next phase
    LAIK_DF_CopyInOut,       // preserve values from previous phase and propagate to next
    LAIK_DF_InitInCopyOut, // Initialize and aggregate, needs reduction operation

    LAIK_DF_Previous,        // derive from previously set flow

} Laik_DataFlow;

// reduction operation to be used in a transition
typedef enum _Laik_ReductionOperation {
    LAIK_RO_None = 0,
    LAIK_RO_Sum, LAIK_RO_Prod,
    LAIK_RO_Min, LAIK_RO_Max,
    LAIK_RO_And, LAIK_RO_Or
} Laik_ReductionOperation;

// a point in an index space
typedef struct _Laik_Index Laik_Index;
struct _Laik_Index {
    int64_t i[3]; // at most 3 dimensions
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
typedef struct _Laik_AccessPhase Laik_AccessPhase;

// set of partitionings to make consistent at the same time
typedef struct _Laik_PartGroup Laik_PartGroup;

// a slice mapped to a task, created by a partitioner
typedef struct _Laik_TaskSlice Laik_TaskSlice;

// calculated partitioning borders, result of a partitioner run
typedef struct _Laik_Partitioning Laik_Partitioning;

// communication requirements when switching partitioning groups
typedef struct _Laik_Transition Laik_Transition;



/*********************************************************************/
/* LAIK API for distributed index spaces
 *********************************************************************/

// is this a reduction?
bool laik_is_reduction(Laik_ReductionOperation redOp);
// do we need to copy values in?
bool laik_do_copyin(Laik_DataFlow flow);
// do we need to copy values out?
bool laik_do_copyout(Laik_DataFlow flow);
// Do we need to init values?
bool laik_do_init(Laik_DataFlow flow);


//--------------------------------------------------------------------------
// LAIK spaces, indexes, slices
//

// create a new index space object (initially invalid)
Laik_Space* laik_new_space(Laik_Instance* inst);

// create a new index space object with an initial size
Laik_Space* laik_new_space_1d(Laik_Instance* i, int64_t s1);
Laik_Space* laik_new_space_2d(Laik_Instance* i, int64_t s1, int64_t s2);
Laik_Space* laik_new_space_3d(Laik_Instance* i,
                              int64_t s1, int64_t s2, int64_t s3);

// free a space with all resources depending on it (e.g. paritionings)
void laik_free_space(Laik_Space* s);

// number of indexes in the space
uint64_t laik_space_size(Laik_Space* s);

// set a space a name, for debug output
void laik_set_space_name(Laik_Space* s, char* n);

// change the size of an index space, eventually triggering a repartitiong
void laik_change_space_1d(Laik_Space* s, int64_t from1, int64_t to1);

// are the indexes equal?
bool laik_index_isEqual(int dims, const Laik_Index* i1, const Laik_Index* i2);

// is the given slice empty?
bool laik_slice_isEmpty(int dims, Laik_Slice* slc);

// get the intersection of 2 slices; return 0 if intersection is empty
Laik_Slice* laik_slice_intersect(int dims, const Laik_Slice* s1, const Laik_Slice* s2);

// expand slice <dst> such that it contains <src>
void laik_slice_expand(int dims, Laik_Slice* dst, Laik_Slice* src);

// is slice <slc1> contained in <slc2>?
bool laik_slice_within_slice(int dims, const Laik_Slice* slc1, const Laik_Slice* slc2);

// is slice within space borders?
bool laik_slice_within_space(const Laik_Slice* slc, const Laik_Space* sp);

// are the slices equal?
bool laik_slice_isEqual(int dims, Laik_Slice* s1, Laik_Slice* s2);

// number of indexes in the slice
uint64_t laik_slice_size(int dims, const Laik_Slice* s);

// get the index slice covered by the space
const Laik_Slice* laik_space_asslice(Laik_Space* space);

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


//--------------------------------------------------------------------------
// LAIK access phase
//

// Create a new access phase for a process group on an index space, using
// a given partitioning. Optionally, a partitioner algorithm may be
// specified as well as another access phase whose change triggers a
// new partitioner run.
//
// The partitioner will be called to calculate a concrete partitioning
// - if partitioning borders are needed but not set yet
//   (on laik_my_slice or when a data container is switched to it)
// - for repartitioning when access phase is migrated to another group
//   (done e.g. when group is shrinked/enlarged from external)
// - whenever a given base partitioning the paritioning of this access
//   phase is linked to changes
Laik_AccessPhase*
laik_new_accessphase(Laik_Group* group, Laik_Space* space,
                      Laik_Partitioner* pr, Laik_AccessPhase* base);

// return partitioner set for a partitioning
Laik_Partitioner* laik_get_partitioner(Laik_AccessPhase* ap);

// set the partitioner to use (can be custom, application-specific)
void laik_set_partitioner(Laik_AccessPhase* ap, Laik_Partitioner* pr);

// get space the access phase is used for
Laik_Space* laik_get_apspace(Laik_AccessPhase* ap);

// get task group used in partitioning
Laik_Group* laik_get_apgroup(Laik_AccessPhase* ap);

// free a partitioning with related resources
void laik_free_accessphase(Laik_AccessPhase* ap);

// get number of slices assigned to calling process for currently active
// paritioning in the given access phase
int laik_phase_my_slicecount(Laik_AccessPhase* ap);

// get number of mappings assigned to calling process for currently active
// paritioning in the given access phase
int laik_phase_my_mapcount(Laik_AccessPhase* ap);

// get number of slices within same mapping with ID <mapNo>, where the
// slices are assigned to the calling process for currently active
// paritioning in the given access phase
int laik_phase_my_mapslicecount(Laik_AccessPhase* ap, int mapNo);

// get slice number <n> from the slices assigned to the calling process
// for the currently active paritioning in the given access phase
Laik_TaskSlice* laik_phase_my_slice(Laik_AccessPhase* ap, int n);

// get slice number <n> within a given map with ID <mapNo> assigned to the
// calling process for the currently active paritioning in the given access phase
Laik_TaskSlice* laik_phase_my_mapslice(Laik_AccessPhase* ap, int mapNo, int n);

// get from/to values for 1d slice with number <n> assigned to this task
// if there is no slice, return 0 and set range to [0;0[ (ie. empty)
Laik_TaskSlice* laik_phase_myslice_1d(Laik_AccessPhase* ap, int n,
                                      int64_t* from, int64_t* to);

// get boundaries [x1;x2[ x [y1;y2[ for 2d slice <n> of this task
// if there is no slice, return 0 and set ranges to [0;0[ (ie. empty)
Laik_TaskSlice* laik_phase_myslice_2d(Laik_AccessPhase* ap, int n,
                                      int64_t* x1, int64_t* x2,
                                      int64_t* y1, int64_t* y2);

// get boundaries [x1;x2[ x [y1;y2[ x [z1;z2[ for 3d slice <n> of this task
// if there is no slice, return 0 and set ranges to [0;0[ (ie. empty)
Laik_TaskSlice* laik_phase_myslice_3d(Laik_AccessPhase* ap, int n,
                                      int64_t* x1, int64_t* x2,
                                      int64_t* y1, int64_t* y2,
                                      int64_t* z1, int64_t* z2);

// applications can attach arbitrary values to a TaskSlice, to be
// passed from application-specific partitioners to slice processing
void* laik_get_slice_data(Laik_TaskSlice*);
void laik_set_slice_data(Laik_TaskSlice*, void* data);

// return the mapping number of this task slice, calculated from tags
// provided by the partitioner
int laik_tslice_get_mapNo(Laik_TaskSlice*);

// get slice of a task slice
Laik_Slice* laik_tslice_get_slice(Laik_TaskSlice*);

// give an access phase a name, for debug output
void laik_set_accessphase_name(Laik_AccessPhase* ap, char* n);


// set new partitioning for given access phase
void laik_phase_set_partitioning(Laik_AccessPhase* ap, Laik_Partitioning* p);

// return currently used partitioning of given access phase
Laik_Partitioning* laik_phase_get_partitioning(Laik_AccessPhase* p);

// force re-run of the configured partitioner for given access phase
Laik_Partitioning* laik_phase_run_partitioner(Laik_AccessPhase* ap);

// get local index from global one. return false if not local
bool laik_index_global2local(Laik_Partitioning*,
                             Laik_Index* global, Laik_Index* local);


// couple an access phase to a program phase.
// switching to a new program phase will result in simultanous switches to
// coupled access phases.
void laik_append_phase(Laik_PartGroup* g, Laik_AccessPhase* ap);

// Calculate communication required for transitioning between
// partitioning borders
Laik_Transition*
laik_calc_transition(Laik_Space* space,
                     Laik_Partitioning* fromP, Laik_DataFlow fromFlow, Laik_ReductionOperation fromRedOp,
                     Laik_Partitioning* toP, Laik_DataFlow toFlow, Laik_ReductionOperation toRedOp);

// Calculate communication for transitioning between partitioning groups
Laik_Transition* laik_calc_transitionG(Laik_PartGroup* from,
                                       Laik_PartGroup* to);

// enforce consistency for the partitioning group, depending on previous
void laik_enforce_consistency(Laik_Instance* i, Laik_PartGroup* g);


// couple different LAIK instances via spaces:
// one partition of calling task in outer space is mapped to inner space
void laik_couple_nested(Laik_Space* outer, Laik_Space* inner);


// migrate a access phase defined on one task group to another group
// if partitioning is set, this only is successful if partitions of
// tasks which are not in the new group are empty.
// return true if migration was successful.
bool laik_migrate_phase(Laik_AccessPhase* p, Laik_Group* newg);

// migrate a access phase defined on one task group to another group
// for the required repartitioning, use the default partitioner
void laik_migrate_and_repartition(Laik_AccessPhase* ap, Laik_Group* newg,
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
bool allowRepartitioning(Laik_AccessPhase* p);

#endif // _LAIK_SPACE_H_

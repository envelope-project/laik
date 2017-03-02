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

#ifndef _LAIK_H_
#define _LAIK_H_

#include <stdint.h>

/*********************************************************************/
/* LAIK enum/struct definitions
 *********************************************************************/

// generic partition types, may need parameters
typedef enum _Laik_PartitionType {
  LAIK_PT_None = 0,
  LAIK_PT_Single,   // only one task has access to all elements
  LAIK_PT_All,      // all tasks have access to all elements
  LAIK_PT_Stripe,   // continous distinct ranges, covering all elements
  LAIK_PT_Halo,     // extend a partition at borders
  LAIK_PT_Neighbor, // extend a partition with neighbor parts
} Laik_PartitionType;

// container types, used in laik_alloc
typedef enum _Laik_DataType {
  LAIK_DT_None = 0,
  LAIK_DT_1D_Double,
  LAIK_DT_2D_Double,
  LAIK_DT_Custom = 100
} Laik_DataType;

// a LAIK task: entity with access to container part
typedef struct _Laik_Task Laik_Task;
struct _Laik_Task {
  int rank;
};

// set of tasks sharing a container
typedef struct _Laik_Group Laik_Group;
struct _Laik_Group {
  int size; // specific case 0: LAIK_WORLD
  Laik_Task t[1]; // variable number of tasks
};

// an index space
typedef struct _Laik_Space Laik_Space;
struct _Laik_Space {
  int dims, size[3]; // at most 3 dimensions
};

// a point in a index space
typedef struct _Laik_Index Laik_Index;
struct _Laik_Index {
  int i[3]; // at most 3 dimensions
};

// a rectangle-shaped slice from an index space
// including coordinates of <from>, excluding <to>
typedef struct _Laik_Slice Laik_Slice;
struct _Laik_Slice {
  Laik_Index from, to;
};

// a partitioning of an index space with
typedef struct _Laik_Partition Laik_Partition;
struct _Laik_Partition {
  int parts;
  Laik_Slice s[1]; // slice borders, one slice per participating task
};

// access phase for a LAIK container
typedef struct _Laik_Phase Laik_Phase;
struct _Laik_Phase {
  char* name; // for debug output
  Laik_Partition* o; // owner with read-write permissions, non-overlapping
  Laik_Partition* r[1]; // read-only partitioning
};

// a LAIK container
typedef struct _Laik_Data Laik_Data;
struct _Laik_Data {
  int elemsize;
  Laik_Space s;    // index space of this container
  Laik_Group g;    // task group sharing this container
  Laik_Phase p[1]; // variable number of partitions
};

// a serialisation order of a LAIK container
typedef struct _Laik_Layout Laik_Layout;
struct _Laik_Layout {
  int dims, order[3]; // at most 3 dimensions  
};

// container part pinned to local memory space
typedef struct _Laik_Pinning Laik_Pinning;
struct _Laik_Pinning {
  Laik_Data* d;
  int p; // phase number in container
  int s; // slice number in partition
  Laik_Layout l; // ordering layout used

  void* base; // start address of pinning
  int count; // number of elements pinned
};

// LAIK communication back-end
typedef struct _Laik_Backend Laik_Backend;
struct _Laik_Backend {
  char* name;
  int (*init)(void); // init callback
  int (*put)(Laik_Task target, int tag, void*, int); // trigger sending
  int (*reg_receiver)(Laik_Task from, int tag, void*, int); // receiver space
  int (*test)(Laik_Task from, int tag); // check if data arrived
};

// LAIK configuration
typedef struct _Laik_Config Laik_Config;
struct _Laik_Config {
  int tasks;
  int id;
  Laik_Backend* backend;
  Laik_Data* d;    // active containers
  Laik_Pinning* p; // active pinnings
};

// LAIK error struct
typedef struct _Laik_Error Laik_Error;
struct _Laik_Error {
  int type;
  char* desc;
};



/* global LAIK variables */

// globally used LAIK configuration
extern Laik_Config laik_config;

// single process backend (ie. dummy)
extern Laik_Backend laik_backend_single;

// all tasks
extern Laik_Group laik_world;

/*********************************************************************/
/* LAIK API
 *********************************************************************/

/**
 * Before using a LAIK container, the LAIK library
 * has to be initialized. Especially, the communication
 * backend needs to be set up for LAIK.
 */
Laik_Error* laik_init(Laik_Backend* backend);

/**
 * Return number of LAIK tasks available
 */
int laik_size();

/**
 * Return rank if calling LAIK task
 */
int laik_myid();

/**
 * Define a LAIK container shared by a LAIK task group.
 * This is a collective operation of all tasks in the group.
 * If no partitioning is set (via laik_setPartition) before
 * before use, default to equal-sized owner STRIPE partitioning.
 */
Laik_Data* laik_alloc(Laik_Group g, Laik_DataType type, uint64_t count);

void laik_fill_double(Laik_Data*, double);

Laik_Pinning* laik_pin(Laik_Data* d, Laik_Layout* l, void** base, uint64_t* count);

void laik_free(Laik_Data*);

void laik_repartition(Laik_Data* d, Laik_PartitionType p);

void laik_finish();


#endif // _LAIK_H_

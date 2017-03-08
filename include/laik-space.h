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

#include "laik.h"

/*********************************************************************/
/* LAIK Spaces - Distributed partitioning of index spaces
 *********************************************************************/


// generic partition types, may need parameters
typedef enum _Laik_PartitionType {
  LAIK_PT_None = 0,
  LAIK_PT_Master,   // only one task has access to all elements
  LAIK_PT_All,      // all tasks have access to all elements
  LAIK_PT_Stripe,   // continous distinct ranges, covering all elements
  LAIK_PT_Halo,     // extend a partition at borders
  LAIK_PT_Neighbor, // extend a partition with neighbor parts
} Laik_PartitionType;

// access permission to partitions
typedef enum _Laik_AccessPermission {
    LAIK_AP_None = 0,
    LAIK_AP_ReadOnly,
    LAIK_AP_WriteOnly, // promises complete overwriting
    LAIK_AP_ReadWrite,
    LAIK_AP_Plus,      // + reduction, multiple writers
    LAIK_AP_Min,       // min reduction, multiple writers
    LAIK_AP_Max        // max reduction, multiple writers
} Laik_AccessPermission;


// a point in an index space
typedef struct _Laik_Index Laik_Index;
struct _Laik_Index {
    int i[3]; // at most 3 dimensions
};

// a rectangle-shaped slice from an index space [from;to[
typedef struct _Laik_Slice Laik_Slice;
struct _Laik_Slice {
  Laik_Index from, to;
};

// a participating task in the distribution of an index space
typedef struct _Laik_Task Laik_Task;

// a task group over which a index space gets distributed
typedef struct _Laik_Group Laik_Group;
struct _Laik_Group;

// an index space (regular and continous, up to 3 dimensions)
typedef struct _Laik_Space Laik_Space;

// a partitioning of an index space with same access permission
typedef struct _Laik_Partitioning Laik_Partitioning;

// set of partitionings to make consistent at the same time
typedef struct _Laik_PartGroup Laik_PartGroup;

// communication requirements when switching partitioning groups
typedef struct _Laik_PartTransition Laik_PartTransition;



/*********************************************************************/
/* LAIK API for distributed index spaces
 *********************************************************************/

Laik_Space* laik_space_create(Laik_Instance*, int dims, ...);

//
// convenience functions
//

// add a new partitioning and instantly switch to
void laik_repartition(Laik_Space* s,
                      Laik_PartitionType pt, Laik_AccessPermission ap);


#endif // _LAIK_SPACE_H_

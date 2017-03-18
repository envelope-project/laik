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
 * use, default to equal-sized owner STRIPE partitioning.
 */
Laik_Data* laik_alloc(Laik_Group* g, Laik_Space* s);
Laik_Data* laik_alloc_1d(Laik_Group* g, int elemsize, uint64_t s1);
Laik_Data* laik_alloc_2d(Laik_Group* g, int elemsize, uint64_t s1, uint64_t s2);

// set a data name, for debug output
void laik_set_data_name(Laik_Data* d, char* n);

// set and enforce a newly created partitioning
void laik_set_new_partitioning(Laik_Data*,
                               Laik_PartitionType, Laik_AccessPermission);

// set and enforce partitioning
void laik_set_partitioning(Laik_Data*, Laik_Partitioning*);


void laik_fill_double(Laik_Data* data, double v);

Laik_Mapping* laik_map(Laik_Data* d, Laik_Layout* l, void** base, uint64_t* count);

void laik_free(Laik_Data*);


#endif // _LAIK_DATA_H_

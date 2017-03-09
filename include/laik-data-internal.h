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

#ifndef _LAIK_DATA_INTERNAL_H_
#define _LAIK_DATA_INTERNAL_H_

#include "laik-data.h"

struct _Laik_Data {
  int elemsize;
  Laik_Space* space; // index space of this container
  Laik_Group* group;

  // default partitioning
  Laik_PartitionType defaultPartitionType;
  Laik_AccessPermission defaultPermission;

  // active partitioning (TODO: multiple may be active)
  Laik_Partitioning* activePartitioning;
  Laik_Mapping* activeMapping;

  // can be set by backend
  void* backend_data;
};

struct _Laik_Layout {
  int dims, order[3]; // at most 3 dimensions  
};

struct _Laik_Mapping {
  Laik_Data* data;
  Laik_Partitioning* partitioning;
  int task; // slice/task number in partition
  Laik_Layout* layout; // ordering layout used

  void* base; // start address of pinning
  int count; // number of elements pinned
};

#endif // _LAIK_DATA_INTERNAL_H_

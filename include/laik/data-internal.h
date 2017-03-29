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

#ifndef _LAIK_INTERNAL_H_
#error "include laik-internal.h instead"
#endif

struct _Laik_Data {
    char* name;
    int id;

    int elemsize;
    Laik_Space* space; // index space of this container
    Laik_Group* group;

    // default partitioning
    Laik_PartitionType defaultPartitionType;
    Laik_AccessBehavior defaultAccess;

    // active partitioning (TODO: multiple may be active)
    Laik_Partitioning* activePartitioning;
    Laik_Mapping* activeMapping;
    Laik_Allocator* allocator;

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
  Laik_Index baseIdx; // global index at base address

  char* base; // start address of mapping
  int count; // number of elements mapped
};

#endif // _LAIK_DATA_INTERNAL_H_

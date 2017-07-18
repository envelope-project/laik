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

#ifndef _LAIK_CORE_INTERNAL_H_
#define _LAIK_CORE_INTERNAL_H_

#ifndef _LAIK_INTERNAL_H_
#error "include laik-internal.h instead"
#endif

#define MAX_GROUPS        10
#define MAX_SPACES        10
#define MAX_PARTITIONINGS 10
#define MAX_DATAS         10
#define MAX_MAPPINGS      10

struct _Laik_Group {
    Laik_Instance* inst;
    int gid;
    int size;
    int myid;
    void* backend_data;

    Laik_Group* parent;
    int ptask[1]; // mapping to task in parent
};

struct _Laik_Instance {
  int size;
  int myid;
  char* mylocation;

  Laik_Backend* backend;
  void* backend_data;

  Laik_Space* firstspace;

  int group_count, data_count, mapping_count;
  Laik_Group* group[MAX_GROUPS];
  Laik_Data* data[MAX_DATAS];
  Laik_Mapping* mapping[MAX_MAPPINGS]; // active mappings

  Laik_Program_Control* control; //new, for iteration numer and program phase
  
};

struct _Laik_Error {
  int type;
  char* desc;
};

#endif // _LAIK_CORE_INTERNAL_H_

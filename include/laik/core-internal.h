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
#define MAX_AGENTS        10



struct _Laik_Task {
    int rank;
};

struct _Laik_Group {
    Laik_Instance* inst;
    int gid;
    int size;
    int myid;
    void* backend_data;

    Laik_Group* parent;
    int* toParent;   // mapping local task IDs to parent task IDs
    int* fromParent; // mapping parent task IDs to local task IDs

    Laik_Partitioning* firstPartitioningForGroup; // linked list head
};

// add/remove partitioning to/from group
void laik_addPartitioningForGroup(Laik_Group* g, Laik_Partitioning* p);
void laik_removePartitioningFromGroup(Laik_Group* g, Laik_Partitioning* p);

struct _Laik_Instance {
    int size;
    int myid;
    char* mylocation;
    char guid[64];

    Laik_Backend* backend;
    void* backend_data;

    Laik_Space* firstSpaceForInstance;

    int group_count, data_count, mapping_count;
    Laik_Group* group[MAX_GROUPS];
    Laik_Data* data[MAX_DATAS];
    Laik_Mapping* mapping[MAX_MAPPINGS]; // active mappings

    Laik_Program_Control* control; //new, for iteration numer and program phase

    // profiling
    bool do_profiling;
    double timer_total, timer_backend;
    double time_total, time_backend;

    // External Control Related
    Laik_RepartitionControl* repart_ctrl;
    
};

// add/remove space to/from instance
void laik_addSpaceForInstance(Laik_Instance* inst, Laik_Space* s);
void laik_removeSpaceFromInstance(Laik_Instance* inst, Laik_Space* s);

void laik_addDataForInstance(Laik_Instance* inst, Laik_Data* d);


struct _Laik_Error {
  int type;
  char* desc;
};

#endif // _LAIK_CORE_INTERNAL_H_

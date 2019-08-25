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

#ifndef LAIK_CORE_INTERNAL_H
#define LAIK_CORE_INTERNAL_H

#include <laik.h>         // for Laik_Instance, Laik_Group
#include <stdbool.h>      // for bool
#include <sys/time.h>     // for struct timeval
#include "definitions.h"  // for MAX_DATAS, MAX_GROUPS, MAX_MAPPINGS

// dynamically generated revision/opt flags information, in info.c
void laik_log_append_info(void);

// key-value store (see below)
typedef struct _Laik_KVNode Laik_KVNode;

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

    int* toLocation;
};


struct _Laik_Instance {
    int size;
    int myid;
    char* mylocation;
    char guid[64];

    // Stores exchanged location information if user requested it using synchronize.
    Laik_KVStore* locationStore;

    // for time logging
    struct timeval init_time;

    const Laik_Backend* backend;
    void* backend_data;

    Laik_Space* firstSpaceForInstance;

    int group_count, data_count, mapping_count;
    Laik_Group* group[MAX_GROUPS];
    Laik_Data* data[MAX_DATAS];
    Laik_Mapping* mapping[MAX_MAPPINGS]; // active mappings

    Laik_Program_Control* control; // for iteration number and program phase

    // profiling
    Laik_Profiling_Controller* profiling;

    // External Control Related
    Laik_RepartitionControl* repart_ctrl;

    // LAIK backend error handler. Gives backends the chance to pass errors back to the user instead of aborting the
    // application
    Laik_Backend_Error_Handler* errorHandler;
};

// add/remove space to/from instance
void laik_addSpaceForInstance(Laik_Instance* inst, Laik_Space* s);
void laik_removeSpaceFromInstance(Laik_Instance* inst, Laik_Space* s);

void laik_addDataForInstance(Laik_Instance* inst, Laik_Data* d);

// synchronize location identifiers
void laik_location_synchronize_data(Laik_Instance *instance, Laik_Group *synchronizationGroup);


struct _Laik_Error {
  int type;
  char* desc;
};


//--------------------------------------------------------
// KV Store
//

struct _Laik_KVS_Entry {
    char* key;
    char* data;
    unsigned int size;
    bool updated;
};

struct _Laik_KVStore {
    Laik_Instance* inst;
    const char* name;

    // KV array
    Laik_KVS_Entry* entry;
    unsigned int size, used;
    // new entries are unsorted, call laik_kvs_sort to enable binary search
    unsigned int sorted_upto;

    // arrays collecting new/change data to send at next sync
    unsigned int myOffSize, myOffUsed;
    unsigned int* myOff;
    unsigned int myDataSize, myDataUsed;
    char* myData;
    // if true, setting values will not be propagated for next sync
    bool in_sync;
};


#endif // LAIK_CORE_INTERNAL_H

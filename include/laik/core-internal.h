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

struct _Laik_Task {
    int rank;
};

struct _Laik_Group {
    Laik_Instance* inst;
    int gid;         // group ID
    int size;        // number of processes in group
    int myid;        // index of this process (in [0;size[ or -1 if not in group)
    void* backend_data;

    Laik_Group *parent, *parent2;
    int maxsize;     // size of allocation for following 3 arrays
    int* locationid; // maps process indexes to location IDs they are bound to
    int* toParent;   // maps process indexes in this group to indexes in parent
    int* fromParent; // maps parent process indexes to indexes in this group
    int* toParent2;  // maps process indexes in this group to indexes in parent
    int* fromParent2;// maps parent process indexes to indexes in this group

    int rc_app;        // used by application?
    int rc_ownprocess; // refererence counter in own process
    int rc_others;     // sum of ref counters of other active processes
};


struct _Laik_Instance {
    // number of process locations, can only grow
    int locations;
    int mylocationid;
    char* mylocation;
    char guid[64];

    // handle to current world, may change (groups themselves are immutable)
    Laik_Group* world;
    // epoch counter, incremented when a new world is activated with a size change
    int epoch;
    // compute phase, for new processes to know where to start (defaults to 0)
    int phase;

    // KV store for exchanging location information
    Laik_KVStore* locationStore;
    // direct access to synchronized location strings (array of size <size>)
    // null if not synced yet; for removed processes entries are null
    char** location;

    // KV stores for LAIK objects
    Laik_KVStore* spaceStore;

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
    
};

// allocate space for a new LAIK instance.
// this is only to be called from backend initialization.
// the world yet is invalid, needs to be created and attached by backend
Laik_Instance* laik_new_instance(const Laik_Backend* b, int size, int myid,
                                 int epoch, int phase,
                                 char* location, void* data);


// add/remove space to/from instance
void laik_addSpaceForInstance(Laik_Instance* inst, Laik_Space* s);
void laik_removeSpaceFromInstance(Laik_Instance* inst, Laik_Space* s);

void laik_addDataForInstance(Laik_Instance* inst, Laik_Data* d);

// synchronize location strings via KVS among processes in current world
void laik_sync_location(Laik_Instance *instance);


struct _Laik_Error {
  int type;
  char* desc;
};


//--------------------------------------------------------
// Elasticity
//

typedef struct _Laik_AddRequest_Entry Laik_AddRequest_Entry;
struct _Laik_AddRequest_Entry {
    char* location;
    bool skip;
    void* backend_data;
    Laik_AddRequest_Entry* next;
};

typedef struct _Laik_RemoveRequest_Entry Laik_RemoveRequest_Entry;
struct _Laik_RemoveRequest_Entry {
    int procIdx; // index into group of resize request
    bool skip;
    void* backend_data;
    Laik_RemoveRequest_Entry* next;
};

typedef struct _Laik_ResizeRequest Laik_ResizeRequest;
struct _Laik_ResizeRequest {
    // based on this group
    Laik_Group* group;
    Laik_AddRequest_Entry* addList;
    Laik_RemoveRequest_Entry* removeList;
};

typedef struct _Laik_ResizeRule Laik_ResizeRule;
struct _Laik_ResizeRule {
    char* name;
    char* options;
    bool singleShot; // if true, will be removed when applied and resize done

    // when checking a resize request, this function is called for each rule
    // registered. <req> is both input and output parameter: existing
    // add/remove request entries can be marked to be skipped and new remove
    // requests can be added.
    // when returning false, the resize request will be cancelled
    bool (*check)(Laik_ResizeRequest* req);

    Laik_ResizeRule* next;
};


//--------------------------------------------------------
// KV Store
//

struct _Laik_KVS_Entry {
    char* key;
    char* value;
    unsigned int vlen;
    bool updated;

    void* data; // custom user data attached to this entry
};

// LAIK-internal KVS change journal
typedef struct _Laik_KVS_Changes Laik_KVS_Changes;
struct _Laik_KVS_Changes {
    int offSize, offUsed;
    int* off;
    int dataSize, dataUsed;
    char* data;
    int entrySize, entryUsed;
    Laik_KVS_Entry* entry;
};

struct _Laik_KVStore {
    Laik_Instance* inst;
    const char* name;

    // KV array
    Laik_KVS_Entry* entry;
    unsigned int size, used;
    // new entries are unsorted, call laik_kvs_sort to enable binary search
    unsigned int sorted_upto;

    laik_kvs_created_func created_func;
    laik_kvs_changed_func changed_func;
    laik_kvs_removed_func removed_func;

    // new/changed data to send at next sync
    Laik_KVS_Changes changes;

    // if true, setting values will not be propagated for next sync
    bool in_sync;
};

// internal API for KVS change journal
Laik_KVS_Changes* laik_kvs_changes_new();
void laik_kvs_changes_init(Laik_KVS_Changes* c);
void laik_kvs_changes_free(Laik_KVS_Changes* c);
void laik_kvs_changes_ensure_size(Laik_KVS_Changes* c, int n, int dlen);
void laik_kvs_changes_set_size(Laik_KVS_Changes* c, int n, int dlen);
void laik_kvs_changes_add(Laik_KVS_Changes* c, char* key, int dlen, char* data,
                          bool do_alloc, bool append_sorted);
void laik_kvs_changes_sort(Laik_KVS_Changes* c);
void laik_kvs_changes_sort(Laik_KVS_Changes* c);
void laik_kvs_changes_sort(Laik_KVS_Changes* c);
void laik_kvs_changes_merge(Laik_KVS_Changes* dst,
                            Laik_KVS_Changes* src1, Laik_KVS_Changes* src2);
void laik_kvs_changes_apply(Laik_KVS_Changes* c, Laik_KVStore* kvs);

#endif // LAIK_CORE_INTERNAL_H

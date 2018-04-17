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

#ifndef _LAIK_CORE_INTERNAL_H_
#define _LAIK_CORE_INTERNAL_H_

#include <laik.h>         // for Laik_Instance, Laik_Group, Laik_AccessPhase
#include <stdbool.h>      // for bool
#include "definitions.h"  // for MAX_DATAS, MAX_GROUPS, MAX_MAPPINGS

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

    Laik_AccessPhase* firstAccessPhaseForGroup; // linked list head
};

// add/remove access phase to/from group
void laik_addAcessPhaseForGroup(Laik_Group* g, Laik_AccessPhase* p);
void laik_removeAccessPhaseForGroup(Laik_Group* g, Laik_AccessPhase* p);

struct _Laik_Instance {
    int size;
    int myid;
    char* mylocation;
    char guid[64];

    // to synchronize internal data among tasks
    Laik_KVNode* kvstore;

    const Laik_Backend* backend;
    void* backend_data;

    Laik_Space* firstSpaceForInstance;

    int group_count, data_count, mapping_count;
    Laik_Group* group[MAX_GROUPS];
    Laik_Data* data[MAX_DATAS];
    Laik_Mapping* mapping[MAX_MAPPINGS]; // active mappings

    Laik_Program_Control* control; //new, for iteration numer and program phase


    // profiling
    Laik_Profiling_Controller* profiling;

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


/*
 * Key-Value store for internal LAIK data, organized in a tree structure
 * Whenever new global objects are created, they are synchronized via the
 * key-value store.
 *
 * This is needed as new joining tasks must become aware of all internal
 * LAIK objects, such as spaces, partitionings, data containers.
 * All objects are identified by names.
 * The objects may be created independently in each task via collective
 * operations. When a global sync is done, the consistency is verified.
 * The store can do aggregation of value lists e.g. to communicate external
 * requests received at arbitrary tasks to all others.
*/

// value types
typedef enum _Laik_KVType {
    LAIK_KV_Invalid = 0,

    // primitive
    LAIK_KV_Struct, LAIK_KV_Int,

    // LAIK objects
    LAIK_KV_Space, LAIK_KV_Data,
    LAIK_KV_Partitioning, LAIK_KV_AccessPhase,

    // Custom
    LAIK_KV_CUSTOM = 100
} Laik_KVType;

typedef struct _Laik_KValue Laik_KValue;
struct _Laik_KValue {
    Laik_KVType type;
    int size;
    int count; // > 1 if an array of values
    void* vPtr;
    bool synched;
};

struct _Laik_KVNode {
    char* name;
    bool synched;
    Laik_KVNode* parent; // if 0, this is root
    Laik_KValue* value;  // if 0, no value attached

    Laik_KVNode* firstChild;
    Laik_KVNode* nextSibling;
};

// create new node. Takes ownership of <name>
Laik_KVNode* laik_kv_newNode(char* name, Laik_KVNode* parent, Laik_KValue* v);
Laik_KVNode* laik_kv_getNode(Laik_KVNode* n, char* path, bool create);
Laik_KValue* laik_kv_setValue(Laik_KVNode* n,
                              char* path, int count, int size, void* value);
int laik_kv_getPathLen(Laik_KVNode* n);
char* laik_kv_getPath(Laik_KVNode* n);
// return the value attached to node reachable by <path> from <n>
Laik_KValue* laik_kv_value(Laik_KVNode* n, char* path);
// iterate over all children of a node <n>, use 0 for <prev> to get first
Laik_KVNode* laik_kv_next(Laik_KVNode* n, Laik_KVNode* prev);
// number of children
int laik_kv_count(Laik_KVNode* n);
// remove child with key, return false if not found
bool laik_kv_remove(Laik_KVNode*n, char* path);
// synchronize KV store
void laik_kv_sync(Laik_Instance* inst);



#endif // _LAIK_CORE_INTERNAL_H_

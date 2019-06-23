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

#ifndef LAIK_CORE_H
#define LAIK_CORE_H

#include <stdbool.h>  // for bool

// configuration for a LAIK instance (there may be multiple)
typedef struct _Laik_Instance Laik_Instance;

// LAIK error struct
typedef struct _Laik_Error Laik_Error;

// backend to use for a LAIK instance
typedef struct _Laik_Backend Laik_Backend;

// a task group over which a index space gets distributed
typedef struct _Laik_Group Laik_Group;

/*********************************************************************/
/* Core LAIK API: task groups and elasticity
 *********************************************************************/

// allocate space for a new LAIK instance
Laik_Instance* laik_new_instance(const Laik_Backend* b, int size, int myid,
                                 char* location, void* data, void *gdata);

//! shut down communication and free resources of this instance
void laik_finalize(Laik_Instance* inst);

//! return a backend-dependant string for the location of the calling task
char* laik_mylocation(Laik_Instance*);

//! create a group to be used in this LAIK instance
Laik_Group* laik_create_group(Laik_Instance*);

//! get default group with all tasks within this LAIK instance
Laik_Group* laik_world(Laik_Instance* i);

//! get instance the group is coming from
Laik_Instance* laik_inst(Laik_Group*);

//! return number of LAIK tasks available (within this instance)
int laik_size(Laik_Group*);

//! return my task id in group <g>. By default, the group master
// has task ID 0, which can be changed.
// Warning: on every shrinking/enlarging, task IDs may change!
int laik_myid(Laik_Group*);

//! create a clone of <g>, derived from <g>.
Laik_Group* laik_clone_group(Laik_Group* g);

// TODO: API for...
// Shrinking controlled by master in a group (collective).
// <len>/<list> only needs to be valid at master, and provides a
// list of task IDs to remove from group. Master cannot be removed.
// For successful shrinking, in any partitionings defined on this group,
// partitions of tasks to be removed need to be empty. Furthermore, any
// groups derived from this group also get shrinked.
// On shrinking, task IDs may change.

//! Returns shrinked group, by removing the <len> tasks in <list>
Laik_Group* laik_new_shrinked_group(Laik_Group* g, int len, int* list);

//! Enlarging controlled by master in a group (collective)
bool laik_enlarge_group(Laik_Group* g, int len, char** list);

// get location ID from process ID in given group
int laik_group_locationid(Laik_Group *group, int id);

// get location string identifier from process ID in given group
// (returns 0 if location strings not synchronized)
char* laik_group_location(Laik_Group *group, int id);

// change the master to task <id>. Return if successful
bool laik_set_master(Laik_Group* g, int id);

//! get the ID if the master task in this group
int laik_get_master(Laik_Group* g);

//! return true if I am master
bool laik_is_master(Laik_Group* g);

// Return true if own process is managed by LAIK instance
// a process may become unmanaged by shrinking the world group.
bool laik_is_managed(Laik_Instance* i);

// Utilities

// These are used for generate a mapping between e.g. MPI Tasks LAIK 
// instances
char* laik_get_guid(Laik_Instance* i);

// profiling

// start profiling for given instance
void laik_enable_profiling(Laik_Instance* i);
void laik_reset_profiling(Laik_Instance* i);
void laik_enable_profiling_file(Laik_Instance* i, const char* filename);
void laik_close_profiling_file(Laik_Instance* i);
double laik_get_total_time(void);
double laik_get_backend_time(void);
void laik_writeout_profile(void);
void laik_profile_printf(const char* msg, ...);
void laik_profile_user_start(Laik_Instance *i);
void laik_profile_user_stop(Laik_Instance* i);



/*********************************************************************/
/* Core LAIK API: Logging
 *********************************************************************/

//! Log levels control whether a log message should be shown to user.
// Default is to only show Error/Panic messages.
// Set environment variable LAIK_LOG to minimum level (integer) to see.
typedef enum _Laik_LogLevel {
    LAIK_LL_None = 0,
    LAIK_LL_Debug,
    LAIK_LL_Info,
    LAIK_LL_Warning, // prefix with "Warning"
    LAIK_LL_Error,   // prefix with "Error"
    LAIK_LL_Panic    // prefix with "Panic" and immediately exit
} Laik_LogLevel;

// increment logging counter used in logging prefix
void laik_log_inc(void);

// log a message, similar to printf
void laik_log(int level, const char* msg, ...) __attribute__ ((format (printf, 2, 3)));

// panic: terminate application
void laik_panic(const char* msg);

// check for log level: return true if given log level will be shown
// use this to guard possibly complex calculations for debug output
bool laik_log_shown(int level);

// to overwrite environment variable LAIK_LOG
void laik_set_loglevel(int level);

// buffered log API: same as laik_log, but allows to build message in steps

// begin a new log message with log level <l>
// returns true if message will be shown; can be used instead of laik_logshown
bool laik_log_begin(int level);
// append to a log message started before with laik_log_begin
void laik_log_append(const char* msg, ...);
// finalize the log message build with laik_log_begin/append and print it
void laik_log_flush(const char* msg, ...);

// Provide a generic LAIK initialization function for programs which don't care
// which backend LAIK actually uses (e.g. the examples).
Laik_Instance* laik_init (int* argc, char*** argv);


/*********************************************************************/
/* KV Store
 *********************************************************************/

// - with explicit request for synchronization
// - multiple KVS possible with own synchronization
// - entries may be shadows
//
// current restrictions:
// - flat
// - sync only among all processues of current world

// a flat KV store
typedef struct _Laik_KVStore Laik_KVStore;

// an entry in a KV Store
typedef struct _Laik_KVS_Entry Laik_KVS_Entry;

// create KVS among processes of current world in instance
Laik_KVStore* laik_kvs_new(const char *name, Laik_Instance* inst);

// free KVS resources
void laik_kvs_free(Laik_KVStore* kvs);

// set a binary data blob as value for key (deep copy, overwrites if key exists)
bool laik_kvs_set(Laik_KVStore* kvs, char* key, unsigned int size, char* data);

// set a null-terminated string as value for key
bool laik_kvs_sets(Laik_KVStore* kvs, char* key, char* str);

// synchronize KV store
void laik_kvs_sync(Laik_KVStore* kvs);

// get data and size via *psize (warning: may get invalid on updates)
char* laik_kvs_get(Laik_KVStore* kvs, char* key, unsigned int *psize);

// get entry for a name
Laik_KVS_Entry* laik_kvs_entry(Laik_KVStore* kvs, char* key);

// get current number of entries
unsigned int laik_kvs_count(Laik_KVStore* kvs);

// get specific entry (warning: order may change an updates)
Laik_KVS_Entry* laik_kvs_getn(Laik_KVStore* kvs, unsigned int n);

// get key of an entry
char* laik_kvs_key(Laik_KVS_Entry* e);

// get size of entry data
unsigned int laik_kvs_size(Laik_KVS_Entry* e);

// get data and size via *psize (warning: may get invalid on updates)
char* laik_kvs_data(Laik_KVS_Entry* e, unsigned int *psize);

// deep copy of entry data, at most <size> bytes, returns bytes copied
unsigned int laik_kvs_copy(Laik_KVS_Entry* e, char* mem, unsigned int size);

// sort KVS entries for faster access (done after sync)
void laik_kvs_sort(Laik_KVStore* kvs);

#endif // LAIK_CORE_H

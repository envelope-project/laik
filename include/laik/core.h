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

#ifndef _LAIK_CORE_H_
#define _LAIK_CORE_H_

#ifndef _LAIK_H_
#error "include laik.h instead"
#endif

#include <stdbool.h>

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
Laik_Instance* laik_new_instance(Laik_Backend* b, int size, int myid,
                                 char* location, void* data);

// shut down communication and free resources of this instance
void laik_finalize(Laik_Instance*inst);

// return a backend-dependant string for the location of the calling task
char* laik_mylocation(Laik_Instance*);

// create a group to be used in this LAIK instance
Laik_Group* laik_create_group(Laik_Instance*);

// get default group with all tasks within this LAIK instance
Laik_Group* laik_world(Laik_Instance* i);

// return number of LAIK tasks available (within this instance)
int laik_size(Laik_Group*);

// return my task id in group <g>. By default, the group master
// has task ID 0, which can be changed.
// Warning: on every shrinking/enlarging, task IDs may change!
int laik_myid(Laik_Group*);

// create a clone of <g>, derived from <g>.
Laik_Group* laik_clone_group(Laik_Group* g);

// TODO: API for...
// Shrinking controlled by master in a group (collective).
// <len>/<list> only needs to be valid at master, and provides a
// list of task IDs to remove from group. Master cannot be removed.
// For successful shrinking, in any partitionings defined on this group,
// partitions of tasks to be removed need to be empty. Furthermore, any
// groups derived from this group also get shrinked.
// On shrinking, task IDs may change.

// Returns shrinked group, by removing the <len> tasks in <list>
Laik_Group* laik_new_shrinked_group(Laik_Group* g, int len, int* list);

// Enlarging controlled by master in a group (collective)
bool laik_enlarge_group(Laik_Group* g, int len, char** list);

// change the master to task <id>. Return if successful
bool laik_set_master(Laik_Group* g, int id);

// get the ID if the master task in this group
int laik_get_master(Laik_Group* g);

// return true if I am master
bool laik_is_master(Laik_Group* g);

// Return true if own process is managed by LAIK instance
// a process may become unmanaged by shrinking the world group.
bool laik_is_managed(Laik_Instance* i);

// profiling

// start profiling for given instance
void laik_enable_profiling(Laik_Instance* i);
void laik_reset_profiling(Laik_Instance* i);
double laik_get_total_time();
double laik_get_backend_time();



/*********************************************************************/
/* Core LAIK API: task groups and elasticity
 *********************************************************************/

// Logging

// Log levels control whether a log message should be shown to user.
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

// log a message, similar to printf
void laik_log(Laik_LogLevel l, const char* msg, ...);

// panic: terminate application
void laik_panic(const char* msg);

// check for log level: return true if given log level will be shown
// use this to guard possibly complex calculations for debug output
bool laik_logshown(Laik_LogLevel l);

// to overwrite environment variable LAIK_LOG
void laik_set_loglevel(Laik_LogLevel l);

// return wall clock time (seconds since 1.1.1971) with sub-second precision
double laik_wtime();

#endif // _LAIK_CORE_H_

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
/* Core LAIK API
 *********************************************************************/

// allocate space for a new LAIK instance
Laik_Instance* laik_new_instance(Laik_Backend* b, int size, int myid, char* location, void* data);

// shut down communication and free resources of this instance
void laik_finalize(Laik_Instance*);

// return a backend-dependant string for the location of the calling task
char* laik_mylocation(Laik_Instance*);

// create a group to be used in this LAIK instance
Laik_Group* laik_create_group(Laik_Instance*);

// get default group with all tasks within this LAIK instance
Laik_Group* laik_world(Laik_Instance* i);

// return number of LAIK tasks available (within this instance)
int laik_size(Laik_Group*);

// return rank of calling LAIK task (within this instance)
int laik_myid(Laik_Group*);


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
void laik_log(Laik_LogLevel l, char* msg, ...);

// check for log level: return true if given log level will be shown
// use this to guard possibly complex calculations for debug output
bool laik_logshown(Laik_LogLevel l);

// to overwrite environment variable LAIK_LOG
void laik_set_loglevel(Laik_LogLevel l);

#endif // _LAIK_CORE_H_

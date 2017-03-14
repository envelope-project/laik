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

// configuration for a LAIK instance (there may be multiple)
typedef struct _Laik_Instance Laik_Instance;

// LAIK error struct
typedef struct _Laik_Error Laik_Error;

// backend to use for a LAIK instance
typedef struct _Laik_Backend Laik_Backend;

// a task group over which a index space gets distributed
typedef struct _Laik_Group Laik_Group;
//struct _Laik_Group;

/*********************************************************************/
/* Core LAIK API
 *********************************************************************/

// allocate space for a new LAIK instance
Laik_Instance* laik_new_instance(Laik_Backend* b, int size, int myid, void* data);

// shut down communication and free resources of this instance
void laik_finalize(Laik_Instance*);

// create a group to be used in this LAIK instance
Laik_Group* laik_create_group(Laik_Instance*);

// get default group with all tasks within this LAIK instance
Laik_Group* laik_world(Laik_Instance* i);

// return number of LAIK tasks available (within this instance)
int laik_size(Laik_Group*);

// return rank of calling LAIK task (within this instance)
int laik_myid(Laik_Group*);

#endif // _LAIK_CORE_H_

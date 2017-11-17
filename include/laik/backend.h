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

#ifndef _LAIK_BACKEND_H_
#define _LAIK_BACKEND_H_

//
// Header file defining the interface between LAIK and a LAIK backend
// This is not public LAIK API, but for backend implementations.
// Do not include this file in LAIK applications!
//

#ifndef _LAIK_INTERNAL_H_
#error "include laik-internal.h instead"
#endif

// "laik-internal.h" includes all the following. This is just to make IDE happy
#include "core.h"
#include "space.h"
#include "space-internal.h"
#include "data-internal.h"

// LAIK communication back-end
struct _Laik_Backend {
  char* name;
  void (*finalize)(Laik_Instance*);

  // prepare the execution of a transition: setup reduction groups etc.
  void (*prepareTransition)(Laik_Data*, Laik_Transition*);

  // free resources allocated in prepareTransition for given transition
  void (*cleanupTransition)(Laik_Data*, Laik_Transition*);

  // execute a transition from mapping in <from> to <to>
  void (*execTransition)(Laik_Data*, Laik_Transition*,
                         Laik_MappingList* from, Laik_MappingList* to);

  // update backend specific data for group if needed
  void (*updateGroup)(Laik_Group*);

  // sync of key-value store
  void (*globalSync)(Laik_Instance*);

  // TODO: async interface: start sending / register receiving / probe
};


// helpers for backends

bool laik_isInGroup(Laik_Transition* t, int group, int task);


#endif // _LAIK_BACKEND_H_

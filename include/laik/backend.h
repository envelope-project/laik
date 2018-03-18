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

#include <laik.h>     // for Laik_Transition, Laik_Data, Laik_Instance, Laik...
#include <stdbool.h>  // for bool


// LAIK communication back-end
struct _Laik_Backend {
  char* name;
  void (*finalize)(Laik_Instance*);

  // Prepare the repeated asynchronous execution of a transition on data
  //
  // can be NULL to state that this communication backend driver does not
  // support transition plans. This implies that only synchronous transitions
  // are supported and executed by calling exec.
  // if NULL, LAIK will never call cleanup/wait/probe (can be NULL, too)
  //
  // this function allows the backend driver to
  // - allocate resources which can be reused when doing the same transition
  //   multiple times (such as memory space for buffers, reductions, lists
  //   of outstanding requests, or resources of the communication library).
  //   For one LAIK data container, only one asynchronous transition can be
  //   active at any time. Thus, resources can be shared among transition
  //   plans for one data container.
  // - prepare an optimized communcation schedule using the pre-allocated
  //   resources
  //
  // TODO:
  // - how to enable merging of plans for different data doing same transition
  // - for reserved and pre-allocated mappings (and fixed data layout),
  //   we could further optimize/specialize our plan:
  //   => extend by providing from/to mappings (instead of providing in exec)
  Laik_TransitionPlan* (*prepare)(Laik_Data*, Laik_Transition*);

  // free resources allocated for a transition plan
  void (*cleanup)(Laik_TransitionPlan*);

  // execute a transition by triggering required communication.
  // a transition plan can be specified, allowing asynchronous execution.
  // else, the transition will be executed synchronously (set <p> to 0).
  // data to send is found in mappings in <from>, to receive in <to>
  // before executing a transition plan again, call wait (see below)
  void (*exec)(Laik_Data*, Laik_Transition*, Laik_TransitionPlan*,
               Laik_MappingList* from, Laik_MappingList* to);

  // wait for outstanding asynchronous communication requests resulting from
  // a call to exec when using a transition plan.
  // If a LAIK container uses multiple mappings, you can wait for finished
  // communication for each mapping separately.
  // Use -1 for <mapNo> to wait for all.
  void (*wait)(Laik_TransitionPlan*, int mapNo);

  // similar to wait, but just probe if any communication for a given mapping
  // already finished (-1 for all)
  bool (*probe)(Laik_TransitionPlan*, int mapNo);

  // update backend specific data for group if needed
  void (*updateGroup)(Laik_Group*);

  // sync of key-value store
  void (*sync)(Laik_Instance*);
};


// helpers for backends

bool laik_isInGroup(Laik_Transition* t, int group, int task);


#endif // _LAIK_BACKEND_H_

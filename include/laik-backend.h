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

#include "laik.h"

// LAIK communication back-end
typedef struct _Laik_Backend Laik_Backend;
struct _Laik_Backend {
  char* name;
  void (*finalize)(Laik_Instance*);
  void (*execTransition)(Laik_Transition*);

  // TODO: async interface: start sending / register receiving / probe
};


#endif // _LAIK_BACKEND_H_

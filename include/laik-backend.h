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
  int (*put)(Laik_Task target, int tag, void*, int); // trigger sending
  int (*reg_receiver)(Laik_Task from, int tag, void*, int); // receiver space
  int (*test)(Laik_Task from, int tag); // check if data arrived
};

Laik_Instance* laik_new_instance(Laik_Backend* b);

#endif // _LAIK_BACKEND_H_

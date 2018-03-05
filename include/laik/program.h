/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 *           (c) 2017 Dai Yang
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

#ifndef LAIK_PROGRAM_H
#define LAIK_PROGRAM_H

#ifndef _LAIK_H_INSIDE_
#error "include laik.h instead"
#endif

// "laik.h" includes the following. This is just to make IDE happy
#include "core.h"

// struct for storing program control information
typedef struct tag_laik_program_control Laik_Program_Control;

// create an empty structure
Laik_Program_Control* laik_program_control_init(void);

// set current iteration number
void laik_set_iteration(Laik_Instance*, int);
// get current iteration number
int laik_get_iteration(Laik_Instance*);

// set current program phase control
void laik_set_phase(Laik_Instance*, int, const char*, void*);
// get current program phase control
void laik_get_phase(Laik_Instance*, int*, const char**, void**);

// provide command line args to LAIK for re-launch
void laik_set_args(Laik_Instance*, int, char**);

// reset iteration
void laik_iter_reset(Laik_Instance*);

#endif //LAIK_PROGRAM_H

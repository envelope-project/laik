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

#ifndef _LAIK_BACKEND_MPI_H_
#define _LAIK_BACKEND_MPI_H_

#include "laik-backend.h"

// MPI backend.

#define mpi_world laik_mpi_world()

// create a LAIK instance for this backend
Laik_Instance* laik_init_mpi(int* argc, char*** argv);

// get the default task group: just this single task
Laik_Group* laik_mpi_world();

#endif // _LAIK_BACKEND_MPI_H_

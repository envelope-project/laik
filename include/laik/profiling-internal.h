/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017 Dai Yang, I10/TUM
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


#ifndef LAIK_PROFILING_INTERNAL
#define LAIK_PROFILING_INTERNAL

#include <stdbool.h>      // for bool
#include "definitions.h"  // for MAX_FILENAME_LENGTH

struct _Laik_Profiling_Controller
{
    // is profiling currently active?
    bool do_profiling;
    bool user_timer_active;

    double timer_total, timer_backend, timer_user;
    double time_total, time_backend, time_user;

    char filename[MAX_FILENAME_LENGTH];
    // to avoid including <stdio.h> here: use void* instead of FILE*
    void* profile_file;
};

#endif // LAIK_PROFILING_INTERNAL

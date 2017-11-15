/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Dai Yang
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

#ifndef LAIK_PROFILING_INTERNAL
#define LAIK_PROFILING_INTERNAL

#include "definitions.h"


struct _Laik_Profiling_Controller{
    bool do_profiling;
    bool user_timer_active;

    double timer_total, timer_backend, timer_user;
    double time_total, time_backend, time_user;

    char filename[MAX_FILENAME_LENGTH];
    void* profile_file;
};


#endif //LAIK_PROFILING_INTERNAL
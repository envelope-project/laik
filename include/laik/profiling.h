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

#ifndef _LAIK_PROFILING_H_
#define _LAIK_PROFILING_H_

#ifndef _LAIK_H_INSIDE_
#error "include laik.h instead"
#endif

// "laik.h" includes the following. This is just to make IDE happy
#include "core.h"

//
// application controlled profiling
//

typedef struct _Laik_Profiling_Controller Laik_Profiling_Controller;

// return wall clock time (seconds since 1.1.1971) with sub-second precision
double laik_wtime();

// called by laik_init
Laik_Profiling_Controller* laik_init_profiling(void);
// called by laik_finalize
void laik_free_profiling(Laik_Instance* i);

// start profiling measurement for given instance
void laik_enable_profiling(Laik_Instance* i);
// reset measured time spans
void laik_reset_profiling(Laik_Instance* i);
// start user-time measurement
void laik_profile_user_start(Laik_Instance *i);
// stop user-time measurement
void laik_profile_user_stop(Laik_Instance* i);
// enable output-to-file mode for use of laik_writeout_profile()
void laik_enable_profiling_file(Laik_Instance* i, const char* filename);
// get LAIK total time for LAIK instance for which profiling is enabled
double laik_get_total_time();
// get LAIK backend time for LAIK instance for which profiling is enabled
double laik_get_backend_time();
// for output-to-file mode, write out meassured times
void laik_writeout_profile();
// disable output-to-file mode, eventually closing yet open file before
void laik_close_profiling_file(Laik_Instance* i);
// print arbitrary text to file in output-to-file mode
void laik_profile_printf(const char* msg, ...);


#endif // _LAIK_PROFILING_H_

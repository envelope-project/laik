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

#ifndef LAIK_PROFILING_H
#define LAIK_PROFILING_H

#ifndef _LAIK_H_
#error "include laik.h instead"
#endif


typedef struct _Laik_Profiling_Controller Laik_Profiling_Controller;
Laik_Profiling_Controller* laik_init_profiling(void);
void laik_free_profiling(Laik_Instance* i);
// profiling

// start profiling for given instance
void laik_enable_profiling(Laik_Instance* i);
void laik_reset_profiling(Laik_Instance* i);
void laik_enable_profiling_file(Laik_Instance* i, const char* filename);
void laik_close_profiling_file(Laik_Instance* i);
double laik_get_total_time();
double laik_get_backend_time();
void laik_writeout_profile();
void laik_profile_printf(const char* msg, ...);
void laik_profile_user_start(Laik_Instance *i);
void laik_profile_user_stop(Laik_Instance* i);


#endif //LAIK_PROFILING_H

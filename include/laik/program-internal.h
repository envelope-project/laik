/*
 * @Author: D. Yang 
 * @Date: 2017-07-06 07:56:23 
 * @Last Modified by: D. Yang
 * @Last Modified time: 2017-07-06 08:23:01
 */

#ifndef LAIK_PROGRAM_INTERNAL
#define LAIK_PROGRAM_INTERNAL

#ifndef _LAIK_INTERNAL_H_
#error "include laik-internal.h instead"
#endif

struct tag_laik_program_control
{
    // current program phase
    void* cur_phase;
    // current iteration number iterations
    int cur_iteration;    
};

#endif //LAIK_PROGRAM_INTERNAL
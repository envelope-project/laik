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
    int cur_phase;
    char* cur_phase_name;
    // user controlled structure
    void* pData;
    // current iteration number iterations
    int cur_iteration;

    // internal counter: incremented on every phase change
    int phase_counter;

    // allow automatic re-launches if set
    int argc;    // 0: invalid
    char** argv;
};

#endif //LAIK_PROGRAM_INTERNAL

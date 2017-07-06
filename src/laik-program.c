/*
 * @Author: D. Yang 
 * @Date: 2017-07-06 08:14:35 
 * @Last Modified by: D. Yang
 * @Last Modified time: 2017-07-06 08:42:59
 */

#include "laik-internal.h"
#include <stdlib.h>
#include <assert.h>

//set current interation number
void laik_set_iteration (
    Laik_Instance* i, 
    int iter
){
    i->control->cur_iteration = iter; 
}
//get current iternation number
int laik_get_iteration (
    Laik_Instance* i
){
    return (i->control->cur_iteration);
}

//set current program phase control
void laik_set_phase (
    Laik_Instance* i, 
    void* phase
){
    i->control->cur_phase = phase;
}
//get current program phase control
void* laik_get_phase (
    Laik_Instance* i
){
    return (i->control->cur_phase);
}
Laik_Program_Control* laik_program_control_init(
    void
){
    Laik_Program_Control* ctrl;
    ctrl = calloc(1, sizeof(Laik_Program_Control));

    assert(ctrl);

    return ctrl;
}

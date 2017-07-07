/*
 * @Author: D. Yang 
 * @Date: 2017-07-06 07:56:03 
 * @Last Modified by: D. Yang
 * @Last Modified time: 2017-07-06 08:42:44
 */

#ifndef LAIK_PROGRAM_H
#define LAIK_PROGRAM_H

#ifndef _LAIK_H_
#error "include laik.h instead"
#endif

//struct for storing program control information
typedef struct tag_laik_program_control Laik_Program_Control;

//create an empty structure
Laik_Program_Control* laik_program_control_init(void);

//set current interation number
void laik_set_iteration (Laik_Instance*, int );
//get current iternation number
int laik_get_iteration (Laik_Instance*);

//set current program phase control
void laik_set_phase (Laik_Instance*, int, char*, void*);
//get current program phase control
void laik_get_phase (Laik_Instance*, int*, char**, void**);

//reset iternation
void laik_iter_reset (Laik_Instance*);

#endif //LAIK_PROGRAM_H
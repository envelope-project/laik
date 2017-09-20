/*
 * @Author: D. Yang 
 * @Date: 2017-07-06 08:14:35 
 * @Last Modified by: D. Yang
 * @Last Modified time: 2017-07-06 08:42:59
 */

#include "laik-internal.h"
#include <stdlib.h>
#include <assert.h>

// set current interation number
void laik_set_iteration(Laik_Instance* i, int iter)
{
    i->control->cur_iteration = iter; 

    if (i->repart_ctrl){
        int c;
        for(c=0; c<i->repart_ctrl->num_agents; c++){
            if(i->repart_ctrl->agents[c]->type == LAIK_AGENT_FT){
                Laik_Ft_Agent* agent = 
                    (Laik_Ft_Agent*) i->repart_ctrl->agents[c];
                if(agent->setiter)
                {
                    agent->setiter(iter);
                }
            }
        }
    }
}

// get current iternation number
int laik_get_iteration(Laik_Instance* i)
{
    return (i->control->cur_iteration);
}

// set current program phase control
void laik_set_phase(Laik_Instance* i,
                    int n_phase, const char* name, void* pData)
{
    i->control->cur_phase = n_phase;
    i->control->cur_phase_name = name;
    i->control->pData = pData;

    i->control->phase_counter++;
}

// get current program phase control
void laik_get_phase(Laik_Instance* i,
                    int* phase, const char** name, void** pData)
{
    *phase = i->control->cur_phase;
    *name = i->control->cur_phase_name;
    *pData = i->control->pData;
}

void laik_set_args(Laik_Instance* i, int argc, char** argv)
{
    i->control->argc = argc;
    i->control->argv = argv;
}

void laik_iter_reset(
    Laik_Instance* i
){
    i->control->cur_iteration = 0;
}

//initialization function
Laik_Program_Control* laik_program_control_init(
    void
){
    Laik_Program_Control* ctrl;
    ctrl = calloc(1, sizeof(Laik_Program_Control));

    assert(ctrl);

    return ctrl;
}

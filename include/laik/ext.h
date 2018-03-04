/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 *               2017 Dai Yang
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

#ifndef _LAIK_EXT_H_
#define _LAIK_EXT_H_

#include <laik/agent.h>

// "laik.h" includes the following. This is just to make IDE happy
#include "core.h"

// LAIK application-external interfaces
//
// currently only repartitioning requests are supported

#define MAX_AGENTS 10

// external control for repartitioning
typedef struct _Laik_RepartitionControl Laik_RepartitionControl;
struct _Laik_RepartitionControl {

    // Finalize function for application
    void (*finalize)(Laik_Instance*);
    
    //List of agents to be queried    
    void* handles[MAX_AGENTS];
    Laik_Agent* agents[MAX_AGENTS];
    int num_agents;
};

void laik_ext_load_agent_from_file (
    Laik_Instance* instance,
    char* path,
    int argc,
    char** argv
);

void laik_ext_load_agent_from_function (
    Laik_Instance* instance,
    laik_agent_init function,
    int argc,
    char** argv
);

void laik_ext_cleanup(Laik_Instance*);
void laik_ext_init(Laik_Instance*);
void laik_get_failed(Laik_Instance*, int*, int**, int);



#endif // _LAIK_EXT_H_

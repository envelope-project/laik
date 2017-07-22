/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
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

#include "laik.h"
#include "interface/agent.h"

// LAIK application-external interfaces
//
// currently only repartitioning requests are supported


// external control for repartitioning
typedef struct _Laik_RepartitionControl Laik_RepartitionControl;
struct _Laik_RepartitionControl {
    // Finalize function to release compute resource
    void (* ext_shut_node)(void* uuid);

    // Finalize function for application
    void (*finalize)(Laik_Instance*);
    
    //List of agents to be queried    
    laik_agent** agents;
    int num_agents;
};

laik_agent* laik_ext_loadagent_static(laik_agent_init_static, int, char**);
laik_agent* laik_ext_loadagent (char* path, int argc, char** argv);
void laik_ext_cleanup(laik_agent* agent);

int laik_allow_repartition(Laik_Instance* instance, Laik_RepartitionControl* ctrl, int num_groups, Laik_Group** groups);



#endif // _LAIK_EXT_H_

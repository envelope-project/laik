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
    // initialization function to be called by Laik
    void (*init)(Laik_Instance*);
    // finalize function to be called by Laik
    void (*finalize)(Laik_Instance*);

    // called when application allows repartitioning
    // can change partitioning policy, force recalculation of borders
    void (*allowRepartitioning)(Laik_Partitioning*);
};

laik_agent* laik_ext_loadagent_static(laik_agent_init_static, int, char**);
laik_agent* laik_ext_loadagent (char* path, int argc, char** argv);
void laik_ext_cleanup(laik_agent* agent);


#endif // _LAIK_EXT_H_

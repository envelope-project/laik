/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"
#include "laik-backend-single.h"

#include <assert.h>
#include <stdlib.h>

static Laik_Backend laik_backend_single = {"Single Task Backend", 0, 0};
static Laik_Instance* single_instance = 0;

Laik_Instance* laik_init_single()
{
    if (!single_instance) {
        single_instance = laik_new_instance(&laik_backend_single, 1, 0, 0);

        // group world
        Laik_Group* g = laik_create_group(single_instance);
        g->inst = single_instance;
        g->gid = 0;
        g->size = 1;
        g->myid = 0;
        g->task[0] = 0;
    }
    return single_instance;
}

Laik_Group* laik_single_world()
{
    if (!single_instance)
        laik_init_single();

    assert(single_instance->group_count > 0);
    return single_instance->group[0];
}

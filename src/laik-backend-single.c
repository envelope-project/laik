/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"
#include "laik-backend-single.h"

#include <assert.h>
#include <stdlib.h>

static Laik_Backend laik_backend_single = {"Single Task Backend", 0, 0, 0, 0};
static Laik_Instance* single_instance = 0;

Laik_Instance* laik_init_single()
{
    if (!single_instance) {
        single_instance = laik_new_instance(&laik_backend_single);
        single_instance->size = 1;
    }
    return single_instance;
}

Laik_Group* laik_single_world()
{
    static Laik_Group* g = 0;

    if (!g) {
        if (!single_instance)
            laik_init_single();

        g = (Laik_Group*) malloc(sizeof(Laik_Group));

        g->inst = single_instance;
        g->gid = 0;
    }
    return g;
}

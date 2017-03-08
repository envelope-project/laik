/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>

Laik_Instance* laik_new_instance(Laik_Backend* b)
{
    Laik_Instance* instance = (Laik_Instance*) malloc(sizeof(Laik_Instance));

    instance->backend = b;
    instance->size = 0; // invalid
    instance->myid = 0;

    instance->space_count = 0;
    instance->data_count = 0;
    instance->mapping_count = 0;

    return instance;
}

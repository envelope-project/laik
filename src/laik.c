/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik.h"

#include <assert.h>
#include <stdlib.h>

// global LAIK configuration
Laik_Config laik_config;

Laik_Error* laik_init(Laik_Backend* backend)
{
    assert(backend != 0);
    laik_config.backend = backend;
    backend->init();
}

Laik_Data* laik_alloc(Laik_DataType type, uint64_t count, Laik_Group g)
{
    Laik_Data* d = (Laik_Data*) malloc(sizeof(Laik_Data));

    return d;
}

Laik_Pinning* laik_mydata(Laik_Data d, Laik_Layout l)
{
    Laik_Pinning* p = (Laik_Pinning*) malloc(sizeof(Laik_Pinning));

    return p;
}

void laik_free(Laik_Data* d)
{
    free(d);
}

void laik_repartition(Laik_data d, Laik_Part p)
{}

void laik_finish()
{}

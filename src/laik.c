/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik.h"

#include <assert.h>
#include <stdlib.h>

// global LAIK configuration
Laik_Config laik_config;

Laik_Backend laik_backend_single = {"Single Process Backend", 0, 0, 0, 0};

Laik_Group laik_world = {0, {0}};


Laik_Error* laik_init(Laik_Backend* b)
{
    assert(b != 0);
    laik_config.backend = b;

    if (b->init)
        b->init();
}

int laik_size()
{
    return 1;
}

int laik_myid()
{
    return 0;
}

Laik_Data* laik_alloc(Laik_Group g, Laik_DataType type, uint64_t count)
{
    Laik_Data* d = (Laik_Data*) malloc(sizeof(Laik_Data));

    return d;
}

void laik_fill_double(Laik_Data* d, double v)
{}

Laik_Pinning* laik_pin(Laik_Data* d, Laik_Layout* l, void** base, uint64_t* count)
{
    Laik_Pinning* p = (Laik_Pinning*) malloc(sizeof(Laik_Pinning));

    return p;
}

void laik_free(Laik_Data* d)
{
    free(d);
}

void laik_repartition(Laik_Data* d, Laik_PartitionType p)
{}

void laik_finish()
{}

/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>

Laik_Data* laik_alloc(Laik_Group* g, Laik_DataType type, uint64_t count)
{
    Laik_Data* d = (Laik_Data*) malloc(sizeof(Laik_Data));

    return d;
}

void laik_set_partitioning(Laik_Data* d,
                           Laik_PartitionType pt, Laik_AccessPermission ap)
{}


void laik_fill_double(Laik_Data* d, double v)
{}

Laik_Mapping* laik_map(Laik_Data* d, Laik_Layout* l, void** base, uint64_t* count)
{
    Laik_Mapping* p = (Laik_Mapping*) malloc(sizeof(Laik_Mapping));

    return p;
}

void laik_free(Laik_Data* d)
{
    free(d);
}


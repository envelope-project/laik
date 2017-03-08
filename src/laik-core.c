/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>


int laik_size(Laik_Instance* i)
{
    return i->size;
}

int laik_myid(Laik_Instance* i)
{
    return i->myid;
}

void laik_finalize(Laik_Instance* i)
{
    assert(i);
    if (i->backend && i->backend->finalize)
        (*i->backend->finalize)(i);
}

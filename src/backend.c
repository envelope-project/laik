/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>

// generic helpers for backends

bool laik_isInGroup(Laik_Transition* t, int group, int task)
{
    // all-group?
    if (group == -1) return true;

    assert(group < t->groupCount);
    TaskGroup* tg = &(t->group[group]);
    for(int i = 0; i < tg->count; i++)
        if (tg->task[i] == task) return true;
    return false;
}

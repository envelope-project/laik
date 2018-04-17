/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017, 2018 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 or later.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>

// generic helpers for backends

bool laik_isInGroup(Laik_Transition* t, int group, int task)
{
    // all-group?
    if (group == -1) return true;

    assert(group < t->subgroupCount);
    TaskGroup* tg = &(t->subgroup[group]);
    for(int i = 0; i < tg->count; i++)
        if (tg->task[i] == task) return true;
    return false;
}

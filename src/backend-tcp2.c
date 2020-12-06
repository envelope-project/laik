/*
 * This file is part of the LAIK library.
 * Copyright (c) 2020 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
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

#ifdef USE_TCP2

#include "laik-internal.h"
#include "laik-backend-tcp2.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define LAIK_PORT 7777
#define LAIK_HOST "localhost"

// forward decl
void laik_tcp2_exec(Laik_ActionSeq* as);
void laik_tcp2_sync(Laik_KVStore* kvs);

// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend = {
    .name = "Dynamic TCP2 Backend",
    .exec = laik_tcp2_exec,
    .sync = laik_tcp2_sync
};

static Laik_Instance* instance = 0;

Laik_Instance* laik_init_tcp2(int* argc, char*** argv)
{
    (void) argc;
    (void) argv;

    if (instance)
        return instance;

    // Get the name of the processor
    char location[256];
    int len;
    if (gethostname(location, 256) != 0)
        len = sprintf(location, "localhost");
    else
        len = strlen(location);
    sprintf(location + len, ":%d", getpid());

    instance = laik_new_instance(&laik_backend, 1, 0, location, 0, 0);
    laik_log(2, "TCP2 backend initialized (at '%s', rank %d/%d)\n",
             location, 0, 1);

    return instance;
}

void laik_tcp2_exec(Laik_ActionSeq* as)
{
    if (as->backend == 0) {
        as->backend = &laik_backend;
        laik_aseq_calc_stats(as);
    }
    // we only support 1 transition exec action
    assert(as->actionCount == 1);
    assert(as->action[0].type == LAIK_AT_TExec);
    Laik_TransitionContext* tc = as->context[0];
    Laik_Data* d = tc->data;
    Laik_Transition* t = tc->transition;
    Laik_MappingList* fromList = tc->fromList;
    Laik_MappingList* toList = tc->toList;

    if (t->redCount > 0) {
        assert(fromList->count == 1);
        assert(toList->count == 1);
        Laik_Mapping* fromMap = &(fromList->map[0]);
        Laik_Mapping* toMap = &(toList->map[0]);
        char* fromBase = fromMap ? fromMap->base : 0;
        char* toBase = toMap ? toMap->base : 0;

        for(int i=0; i < t->redCount; i++) {
            assert(d->space->dims == 1);
            struct redTOp* op = &(t->red[i]);
            int64_t from = op->slc.from.i[0];
            int64_t to   = op->slc.to.i[0];
            assert(fromBase != 0);
            assert(laik_trans_isInGroup(t, op->outputGroup, t->group->myid));
            assert(toBase != 0);
            assert(to > from);

            laik_log(1, "TCP2 reduce: "
                        "from %lld, to %lld, elemsize %d, base from/to %p/%p\n",
                     (long long int) from, (long long int) to,
                     d->elemsize, (void*) fromBase, (void*) toBase);

            memcpy(toBase, fromBase, (to-from) * fromMap->data->elemsize);
        }
    }

    // TODO: currently no send/recv actions supported
    assert(t->recvCount == 0);
    assert(t->sendCount == 0);
}

void laik_tcp2_sync(Laik_KVStore* kvs)
{
    // nothing to do
    (void) kvs;
}

#endif // USE_TCP2

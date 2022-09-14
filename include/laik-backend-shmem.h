/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
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

#ifndef LAIK_BACKEND_SHMEM_H
#define LAIK_BACKEND_SHMEM_H

#include "laik.h"

// SHMEM backend.

// create a LAIK instance for this backend.
// if application already called MPI_Init, pass 0 for args
// returns the same object if called multiple times
Laik_Instance* laik_init_shmem(int* argc, char*** argv);

int laik_shmem_secondary_init(int primaryRank, int primarySize, int (*send)(int *, int, int),
                         int (*recv)(int *, int, int));
int laik_shmem_secondary_finalize();
bool laik_aseq_replaceWithShmemCalls(Laik_ActionSeq *as);
bool laik_shmem_secondary_exec(Laik_ActionSeq *as, Laik_Action *a);
bool laik_shmem_log_action(Laik_Action *action);

#endif // LAIK_BACKEND_SHMEM_H

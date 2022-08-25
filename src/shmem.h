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

#ifndef SHMEM_H
#define SHMEM_H

#define SHMEM_FAILURE -1
#define SHMEM_SUCCESS 0
#define SHMEM_SHMGET_FAILED 1
#define SHMEM_SHMAT_FAILED 2
#define SHMEM_SHMDT_FAILED 3
#define SHMEM_SHMCTL_FAILED 4
#define SHMEM_RECV_BUFFER_TOO_SMALL 5

#define SHMEM_MAX_ERROR_STRING 100

int shmem_init();

int shmem_comm_size(int *sizePtr);

int shmem_comm_rank(int *rankPtr);

int shmem_comm_colour(int *colourPtr);

int shmem_get_identifier(int *ident);

int shmem_send(const void *buffer, int count, int datatype, int recipient);

int shmem_recv(void *buffer, int count, int datatype, int sender, int *recieved);

int shmem_error_string(int error, char *str);

int shmem_secondary_init(int primaryRank, int primarySize, int (*send)(int *, int, int),
                         int (*recv)(int *, int, int));

int shmem_get_colours(int *buf);

int shmem_get_secondaryRanks(int *buf);

int shmem_finalize();

#endif
/*
 * This file is part of the LAIK library.
 * Copyright (c) 2018 Alexander Kurtz <alexander@kurtz.be>
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

#pragma once

#include <stddef.h>  // for size_t, NULL

typedef enum {
    LAIK_TCP_MINIMPI_DOUBLE,
    LAIK_TCP_MINIMPI_FLOAT,
    LAIK_TCP_MINIMPI_INT64,
    LAIK_TCP_MINIMPI_INT32,
    LAIK_TCP_MINIMPI_INT8,
    LAIK_TCP_MINIMPI_UINT64,
    LAIK_TCP_MINIMPI_UINT32,
    LAIK_TCP_MINIMPI_UINT8
} Laik_Tcp_MiniMpiType;

typedef enum {
    LAIK_TCP_MINIMPI_SUM,
    LAIK_TCP_MINIMPI_PROD,
    LAIK_TCP_MINIMPI_MIN,
    LAIK_TCP_MINIMPI_MAX,
    LAIK_TCP_MINIMPI_LAND,
    LAIK_TCP_MINIMPI_LOR,
} Laik_Tcp_MiniMpiOp;

typedef size_t Laik_Tcp_MiniMpiStatus;

typedef struct Laik_Tcp_MiniMpiComm Laik_Tcp_MiniMpiComm;

extern Laik_Tcp_MiniMpiComm* LAIK_TCP_MINIMPI_COMM_WORLD;

#define LAIK_TCP_MINIMPI_IN_PLACE           (NULL)
#define LAIK_TCP_MINIMPI_MAX_ERROR_STRING   (1<<16)
#define LAIK_TCP_MINIMPI_MAX_PROCESSOR_NAME (1<<16)
#define LAIK_TCP_MINIMPI_SUCCESS            (0)
#define LAIK_TCP_MINIMPI_UNDEFINED          (-1)

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_allreduce (const void* input_buffer, void* output_buffer, int elements, Laik_Tcp_MiniMpiType datatype, Laik_Tcp_MiniMpiOp op, const Laik_Tcp_MiniMpiComm* comm);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_barrier (const Laik_Tcp_MiniMpiComm* comm);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_bcast (void* buffer, int elements, Laik_Tcp_MiniMpiType datatype, size_t root, const Laik_Tcp_MiniMpiComm* comm);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_comm_dup (const Laik_Tcp_MiniMpiComm* comm, Laik_Tcp_MiniMpiComm** new_communicator);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_comm_rank (const Laik_Tcp_MiniMpiComm* comm, int* rank);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_comm_size (const Laik_Tcp_MiniMpiComm* comm, int* size);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_comm_split (const Laik_Tcp_MiniMpiComm* comm, int color, int hint, Laik_Tcp_MiniMpiComm** new_communicator);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_comm_eliminate(const struct Laik_Tcp_MiniMpiComm* comm, int count, const int* rankStatus, int selfIndex, struct Laik_Tcp_MiniMpiComm** newCommunicator);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_error_string (int error_code, char *string, int *result_length);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_finalize (void);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_get_count (const Laik_Tcp_MiniMpiStatus* status, Laik_Tcp_MiniMpiType datatype, int* elements);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_get_processor_name (char* name, int* result_length);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_initialized (int* flag);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_init (int* argc, char*** argv);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_recv (void* buffer, int elements, Laik_Tcp_MiniMpiType datatype, size_t sender, int tag, const Laik_Tcp_MiniMpiComm* comm, Laik_Tcp_MiniMpiStatus* status);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_reduce (const void* input_buffer, void* output_buffer, int elements, Laik_Tcp_MiniMpiType datatype, Laik_Tcp_MiniMpiOp op, size_t root, const Laik_Tcp_MiniMpiComm* comm);

__attribute__ ((warn_unused_result))
int laik_tcp_minimpi_send (const void* buffer, int elements, Laik_Tcp_MiniMpiType datatype, size_t receiver, int tag, const Laik_Tcp_MiniMpiComm* comm);

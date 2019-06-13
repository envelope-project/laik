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

#include "minimpi.h"  // for LAIK_TCP_MINIMPI_COMM_WORLD, LAIK_TCP_MINIMPI_IN_PLACE, LAIK_TCP_MINIMPI_M...

#define MPI_DOUBLE   LAIK_TCP_MINIMPI_DOUBLE
#define MPI_FLOAT    LAIK_TCP_MINIMPI_FLOAT
#define MPI_INT64_T  LAIK_TCP_MINIMPI_INT64
#define MPI_INT32_T  LAIK_TCP_MINIMPI_INT32
#define MPI_INT8_T   LAIK_TCP_MINIMPI_INT8
#define MPI_UINT64_T LAIK_TCP_MINIMPI_UINT64
#define MPI_UINT32_T LAIK_TCP_MINIMPI_UINT32
#define MPI_UINT8_T  LAIK_TCP_MINIMPI_UINT8
typedef Laik_Tcp_MiniMpiType MPI_Datatype;

#define MPI_SUM  LAIK_TCP_MINIMPI_SUM
#define MPI_PROD LAIK_TCP_MINIMPI_PROD
#define MPI_MIN  LAIK_TCP_MINIMPI_MIN
#define MPI_MAX  LAIK_TCP_MINIMPI_MAX
#define MPI_LAND LAIK_TCP_MINIMPI_LAND
#define MPI_LOR  LAIK_TCP_MINIMPI_LOR
typedef Laik_Tcp_MiniMpiOp MPI_Op;

typedef Laik_Tcp_MiniMpiStatus MPI_Status;

typedef Laik_Tcp_MiniMpiComm* MPI_Comm;

#define MPI_COMM_WORLD         LAIK_TCP_MINIMPI_COMM_WORLD
#define MPI_IN_PLACE           LAIK_TCP_MINIMPI_IN_PLACE
#define MPI_MAX_ERROR_STRING   LAIK_TCP_MINIMPI_MAX_ERROR_STRING
#define MPI_MAX_PROCESSOR_NAME LAIK_TCP_MINIMPI_MAX_PROCESSOR_NAME
#define MPI_SUCCESS            LAIK_TCP_MINIMPI_SUCCESS
#define MPI_UNDEFINED          LAIK_TCP_MINIMPI_UNDEFINED

#define MPI_Allreduce          laik_tcp_minimpi_allreduce
#define MPI_Barrier            laik_tcp_minimpi_barrier
#define MPI_Bcast              laik_tcp_minimpi_bcast
#define MPI_Comm_dup           laik_tcp_minimpi_comm_dup
#define MPI_Comm_rank          laik_tcp_minimpi_comm_rank
#define MPI_Comm_size          laik_tcp_minimpi_comm_size
#define MPI_Comm_split         laik_tcp_minimpi_comm_split
#define MPI_Comm_eliminate     laik_tcp_minimpi_comm_eliminate
#define MPI_Error_string       laik_tcp_minimpi_error_string
#define MPI_Finalize           laik_tcp_minimpi_finalize
#define MPI_Get_count          laik_tcp_minimpi_get_count
#define MPI_Get_processor_name laik_tcp_minimpi_get_processor_name
#define MPI_Initialized        laik_tcp_minimpi_initialized
#define MPI_Init               laik_tcp_minimpi_init
#define MPI_Recv               laik_tcp_minimpi_recv
#define MPI_Reduce             laik_tcp_minimpi_reduce
#define MPI_Send               laik_tcp_minimpi_send

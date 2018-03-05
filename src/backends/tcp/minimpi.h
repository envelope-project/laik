#pragma once

#include <stddef.h>  // for size_t, NULL

typedef enum {
    LAIK_TCP_MINIMPI_DOUBLE,
    LAIK_TCP_MINIMPI_FLOAT,
} Laik_Tcp_MiniMpiType;

typedef enum {
    LAIK_TCP_MINIMPI_SUM
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

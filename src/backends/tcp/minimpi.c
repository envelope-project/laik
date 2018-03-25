#include "minimpi.h"
#include <endian.h>     // for htole64, htobe64
#include <glib.h>       // for g_autoptr, g_bytes_get_size, GBytes_autoptr
#include <stdbool.h>    // for false
#include <stdint.h>     // for uint64_t, int64_t, SIZE_MAX
#include <stdio.h>      // for snprintf
#include <string.h>     // for memcpy, strnlen
#include <unistd.h>     // for gethostname, getpid
#include "config.h"     // for Laik_Tcp_Config, laik_tcp_config, Laik_Tcp_Co...
#include "debug.h"      // for laik_tcp_always, laik_tcp_debug
#include "errors.h"     // for laik_tcp_errors_push, laik_tcp_errors_new
#include "messenger.h"  // for laik_tcp_messenger_get, laik_tcp_messenger_push
#include "socket.h"     // for laik_tcp_socket_new, ::LAIK_TCP_SOCKET_TYPE_S...

// Type definitions

struct Laik_Tcp_MiniMpiComm {
    GArray*  tasks;      // Mapping from per-communicator ranks to world ranks
    size_t   rank;       // Our own rank in this communicator
    size_t   generation; // The number of generations to the world communicator
};

typedef struct __attribute__ ((packed)) {
    int64_t  color;
    int64_t  hint;
    uint64_t rank;
} Laik_Tcp_Split;

typedef enum {
    TYPE_BARRIER      = 0xaa,
    TYPE_BROADCAST    = 0xbb,
    TYPE_REDUCE       = 0xcc,
    TYPE_SEND_RECEIVE = 0xdd,
    TYPE_SPLIT        = 0xee,
} MessageType;

// Internal variables

static GHashTable*         flows     = NULL;
static Laik_Tcp_Messenger* messenger = NULL;

// Internal functions

static void laik_tcp_minimpi_destroy (void* data) {
    g_bytes_unref (data);
}

__attribute__ ((warn_unused_result))
static Laik_Tcp_MiniMpiComm* laik_tcp_minimpi_new (GArray* tasks, size_t rank, size_t generation) {
    Laik_Tcp_MiniMpiComm* this = g_new0 (Laik_Tcp_MiniMpiComm, 1);

    *this = (Laik_Tcp_MiniMpiComm) {
        .tasks      = tasks,
        .rank       = rank,
        .generation = generation,
    };

    return this;
}

__attribute__ ((warn_unused_result))
static size_t laik_tcp_minimpi_lookup (const Laik_Tcp_MiniMpiComm* comm, size_t rank) {
    laik_tcp_always (comm);
    laik_tcp_always (comm->tasks);
    laik_tcp_always (rank < comm->tasks->len);

    return g_array_index (comm->tasks, size_t, rank);
}

__attribute__ ((warn_unused_result))
static GBytes* laik_tcp_minimpi_header (uint64_t generation, uint64_t type, uint64_t sender, uint64_t receiver, uint64_t tag) {
    laik_tcp_always (flows);

    const uint64_t data[] = {
        htole64 (generation),
        htole64 (type),
        htole64 (sender),
        htole64 (receiver),
        htole64 (tag),
        htole64 (0),
    };

    GBytes* result = g_bytes_new (&data, sizeof (data));

    uint64_t* serial = g_hash_table_lookup (flows, result);

    if (serial) {
        ((uint64_t*) g_bytes_get_data (result, NULL))[5] = htobe64 (++*serial);
    } else {
        g_hash_table_insert (flows, g_bytes_ref (result), g_new0 (uint64_t, 1));
    }

    return result;
}

__attribute__ ((warn_unused_result))
static int laik_tcp_minimpi_error (Laik_Tcp_Errors* errors) {
    laik_tcp_always (errors);

    if (laik_tcp_errors_present (errors)) {
        return g_quark_from_string (laik_tcp_errors_show (errors));
    } else {
        return LAIK_TCP_MINIMPI_SUCCESS;
    }
}

__attribute__ ((warn_unused_result))
static int laik_tcp_split_compare (const void* a, const void* b) {
    const Laik_Tcp_Split* x = a;
    const Laik_Tcp_Split* y = b;

    return x->hint - y->hint;
}

static size_t laik_tcp_minimpi_sizeof (const Laik_Tcp_MiniMpiType datatype, Laik_Tcp_Errors* errors) {
    laik_tcp_always (errors);

    switch (datatype) {
        case LAIK_TCP_MINIMPI_DOUBLE:
            return sizeof (double);
        case LAIK_TCP_MINIMPI_FLOAT:
            return sizeof (float);
        default:
            laik_tcp_errors_push (errors, __func__, 0, "Invalid MPI datatype %d", datatype);
            return 0;
    }
}

static void laik_tcp_minimpi_combine (void* buffer, const void* data, const size_t elements, const Laik_Tcp_MiniMpiType datatype, const Laik_Tcp_MiniMpiOp op, Laik_Tcp_Errors* errors) {
    laik_tcp_always (buffer);
    laik_tcp_always (data);
    laik_tcp_always (errors);

    switch (op) {
        case LAIK_TCP_MINIMPI_SUM:
            switch (datatype) {
                case LAIK_TCP_MINIMPI_DOUBLE:
                    {
                        double* b = buffer;
                        const double* d = data;
                        for (size_t i = 0; i < elements; i++) {
                            b[i] += d[i];
                        }
                    }
                    break;
                case LAIK_TCP_MINIMPI_FLOAT:
                    {
                        float* b = buffer;
                        const float* d = data;
                        for (size_t i = 0; i < elements; i++) {
                            b[i] += d[i];
                        }
                    }
                    break;
                default:
                    laik_tcp_errors_push (errors, __func__, 0, "Invalid MPI datatype %d", datatype);
                    break;
            }
            break;
        default:
            laik_tcp_errors_push (errors, __func__, 1, "Invalid MPI operation %d", op);
            break;
    }
}

// Public variables

Laik_Tcp_MiniMpiComm* LAIK_TCP_MINIMPI_COMM_WORLD = NULL;

// Public functions

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Allreduce.html
int laik_tcp_minimpi_allreduce (const void* input_buffer, void* output_buffer, const int elements, const Laik_Tcp_MiniMpiType datatype, const Laik_Tcp_MiniMpiOp op, const Laik_Tcp_MiniMpiComm* comm) {
    laik_tcp_always (comm);

    const size_t root = 0;

    const int reduce_result = laik_tcp_minimpi_reduce (input_buffer, output_buffer, elements, datatype, op, root, comm);
    if (reduce_result != LAIK_TCP_MINIMPI_SUCCESS) {
        return reduce_result;
    }

    const int bcast_result = laik_tcp_minimpi_bcast (output_buffer, elements, datatype, root, comm);
    if (bcast_result != LAIK_TCP_MINIMPI_SUCCESS) {
        return bcast_result;
    }

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/latest/www/www3/MPI_Barrier.html
int laik_tcp_minimpi_barrier (const Laik_Tcp_MiniMpiComm* comm) {
    laik_tcp_always (comm);

    laik_tcp_debug ("MPI_Barrier entered by task %zu", comm->rank);

    const size_t master = 0;

    if (comm->rank == master) {
        // Receive the ping message from all slaves
        for (size_t slave = 1; slave < comm->tasks->len; slave++) {
            g_autoptr (GBytes) ping_header = laik_tcp_minimpi_header (comm->generation, TYPE_BARRIER, slave, master, 0);
            g_autoptr (GBytes) ping_body   = laik_tcp_messenger_get (messenger, laik_tcp_minimpi_lookup (comm, slave), ping_header);
            (void) ping_body;
            laik_tcp_debug ("Master got ping from slave %zu", slave);
        }

        // Synchronously (!) send the pong to all slaves
        for (size_t slave = 1; slave < comm->tasks->len; slave++) {
            g_autoptr (GBytes) pong_header = laik_tcp_minimpi_header (comm->generation, TYPE_BARRIER, master, slave, 0);
            g_autoptr (GBytes) pong_body   = g_bytes_new (NULL, 0);
            laik_tcp_messenger_send (messenger, laik_tcp_minimpi_lookup (comm, slave), pong_header, pong_body);
            laik_tcp_debug ("Master sent pong to slave %zu", slave);
        }
    } else {
        // Send the ping message to the master
        g_autoptr (GBytes) ping_header = laik_tcp_minimpi_header (comm->generation, TYPE_BARRIER, comm->rank, master, 0);
        g_autoptr (GBytes) ping_body   = g_bytes_new (NULL, 0);
        laik_tcp_messenger_push (messenger, laik_tcp_minimpi_lookup (comm, master), ping_header, ping_body);
        laik_tcp_debug ("Slave %zu sent ping to master", comm->rank);

        // Receive the pong message from the master
        g_autoptr (GBytes) pong_header = laik_tcp_minimpi_header (comm->generation, TYPE_BARRIER, master, comm->rank, 0);
        g_autoptr (GBytes) pong_body   = laik_tcp_messenger_get (messenger, laik_tcp_minimpi_lookup (comm, master), pong_header);
        (void) pong_body;
        laik_tcp_debug ("Slave %zu got pong from master", comm->rank);
    }

    laik_tcp_debug ("MPI_Barrier completed by task %zu", comm->rank);

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/latest/www/www3/MPI_Bcast.html
int laik_tcp_minimpi_bcast (void* buffer, const int elements, const Laik_Tcp_MiniMpiType datatype, const size_t root, const Laik_Tcp_MiniMpiComm* comm) {
    laik_tcp_always (comm);
    laik_tcp_always (root < comm->tasks->len);

    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

    const size_t size = elements * laik_tcp_minimpi_sizeof (datatype, errors);
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to determine size of data type");
        return laik_tcp_minimpi_error (errors);
    }

    if (comm->rank == root) {
        g_autoptr (GBytes) body = g_bytes_new (buffer, size);

        for (size_t receiver = 0; receiver < comm->tasks->len; receiver++) {
            if (receiver != comm->rank) {
                g_autoptr (GBytes) header = laik_tcp_minimpi_header (comm->generation, TYPE_BROADCAST, root, receiver, 0);

                laik_tcp_messenger_push (messenger, laik_tcp_minimpi_lookup (comm, receiver), header, body);
            }
        }
    } else {
        g_autoptr (GBytes) header = laik_tcp_minimpi_header (comm->generation, TYPE_BROADCAST, root, comm->rank, 0);
        g_autoptr (GBytes) body   = laik_tcp_messenger_get (messenger, laik_tcp_minimpi_lookup (comm, root), header);

        if (g_bytes_get_size (body) != size) {
            laik_tcp_errors_push (errors, __func__, 1, "Broadcast from root task %zu was %zu bytes, expected %zu bytes", root, g_bytes_get_size (body), size);
            return laik_tcp_minimpi_error (errors);
        }

        memcpy (buffer, g_bytes_get_data (body, NULL), size);
    }

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Comm_dup.html
int laik_tcp_minimpi_comm_dup (const Laik_Tcp_MiniMpiComm* comm, Laik_Tcp_MiniMpiComm** new_communicator) {
    laik_tcp_always (comm);

    *new_communicator = laik_tcp_minimpi_new (g_array_ref (comm->tasks), comm->rank, comm->generation + 1);

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Comm_rank.html
int laik_tcp_minimpi_comm_rank (const Laik_Tcp_MiniMpiComm* comm, int* rank) {
    laik_tcp_always (comm);

    *rank = comm->rank;

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Comm_size.html
int laik_tcp_minimpi_comm_size (const Laik_Tcp_MiniMpiComm* comm, int* size) {
    laik_tcp_always (comm);

    *size = comm->tasks->len;

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Comm_split.html
int laik_tcp_minimpi_comm_split (const Laik_Tcp_MiniMpiComm* comm, const int color, const int hint, Laik_Tcp_MiniMpiComm** new_communicator) {
    laik_tcp_always (comm);

    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

    // Create our own split info structure
    const Laik_Tcp_Split split = {
        .color = color,
        .hint  = hint,
        .rank  = comm->rank,
    };

    // Create a serialized version of our split info structure
    g_autoptr (GBytes) split_bytes = g_bytes_new (&split, sizeof (split));

    // Create an array to hold all the split info structures
    g_autoptr (GArray) splits = g_array_new (false, false, sizeof (Laik_Tcp_Split));

    // Insert ourselves into the array
    g_array_append_vals (splits, &split, 1);

    // Iterate over all peers
    for (size_t sender = 0; sender < comm->tasks->len; sender++) {
        if (sender == comm->rank) {
            // It's our turn to send our split info structure all other peers
            for (size_t receiver = 0; receiver < comm->tasks->len; receiver++) {
                if (receiver != comm->rank) {
                    g_autoptr (GBytes) header = laik_tcp_minimpi_header (comm->generation, TYPE_SPLIT, sender, receiver, 0);
                    laik_tcp_messenger_push (messenger, laik_tcp_minimpi_lookup (comm, receiver), header, split_bytes);
                }
            }
        } else {
            // It's not our turn to send, instead we should receive data
            g_autoptr (GBytes) header = laik_tcp_minimpi_header (comm->generation, TYPE_SPLIT, sender, comm->rank, 0);
            g_autoptr (GBytes) body   = laik_tcp_messenger_get (messenger, laik_tcp_minimpi_lookup (comm, sender), header);

            if (g_bytes_get_size (body) != sizeof (Laik_Tcp_Split)) {
                laik_tcp_errors_push (errors, __func__, 0, "Task %zu sent %zu bytes when splitting, expected %zu bytes", sender, g_bytes_get_size (body), sizeof (Laik_Tcp_Split));
                return laik_tcp_minimpi_error (errors);
            }

            g_array_append_vals (splits, g_bytes_get_data (body, NULL), 1);
        }
    }

    // Create a new task list
    g_autoptr (GArray) tasks = g_array_new (false, false, sizeof (size_t));

    // Sort all the split info structures (including our own!) by hint
    g_array_sort (splits, laik_tcp_split_compare);

    // Initialize our new rank with something invalid
    size_t new_local_rank = SIZE_MAX;

    // Iterate over all the split info structures
    for (size_t i = 0; i < splits->len; i++) {
        const Laik_Tcp_Split split = g_array_index (splits, Laik_Tcp_Split, i);

        // If we have reached our own split info, we have found our new rank
        if (split.rank == comm->rank) {
            new_local_rank = tasks->len;
        }

        // If it's us or somebody with the same color, add them to the task list
        if (split.rank == comm->rank || (split.color == color && split.color != LAIK_TCP_MINIMPI_UNDEFINED)) {
            const size_t world_rank = laik_tcp_minimpi_lookup (comm, split.rank);
            g_array_append_vals (tasks, &world_rank, 1);
        }
    }

    // Make sure we have set our new local rank to something valid
    laik_tcp_always (new_local_rank < tasks->len);

    // Construct the new communicator
    *new_communicator = laik_tcp_minimpi_new (g_steal_pointer (&tasks), new_local_rank, comm->generation + 1);

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Error_string.html
int laik_tcp_minimpi_error_string (int error_code, char *string, int *result_length) {
    laik_tcp_always (string);
    laik_tcp_always (result_length);

    size_t bytes = snprintf (string, LAIK_TCP_MINIMPI_MAX_ERROR_STRING, "%s", g_quark_to_string (error_code));
    laik_tcp_always (bytes < LAIK_TCP_MINIMPI_MAX_ERROR_STRING);
    *result_length = bytes;

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Finalize.html
int laik_tcp_minimpi_finalize (void) {
    laik_tcp_always (flows);
    laik_tcp_always (messenger);

    // Enforce global synchronization before going down
    const int barrier_result = laik_tcp_minimpi_barrier (LAIK_TCP_MINIMPI_COMM_WORLD);
    if (barrier_result != LAIK_TCP_MINIMPI_SUCCESS) {
        return barrier_result;
    }

    LAIK_TCP_MINIMPI_COMM_WORLD = NULL;

    g_hash_table_unref (flows);
    flows = NULL;

    laik_tcp_messenger_free (messenger);
    messenger = NULL;

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Get_count.html
int laik_tcp_minimpi_get_count (const Laik_Tcp_MiniMpiStatus* status, const Laik_Tcp_MiniMpiType datatype, int* count){
    laik_tcp_always (status);
    laik_tcp_always (count);

    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

    const size_t size = laik_tcp_minimpi_sizeof (datatype, errors);
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to determine size of data type");
        return laik_tcp_minimpi_error (errors);
    }

    *count = *status / size;

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Get_processor_name.html
int laik_tcp_minimpi_get_processor_name (char* name, int* result_length) {
    laik_tcp_always (name);
    laik_tcp_always (result_length);

    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

    char hostname[1024];
    if (gethostname (hostname, sizeof (hostname)) != 0 || strnlen (name, sizeof (hostname)) == sizeof (hostname)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to determine hostname");
        return laik_tcp_minimpi_error (errors);
    }

    const size_t bytes = snprintf (name, LAIK_TCP_MINIMPI_MAX_PROCESSOR_NAME, "%s:%d", hostname, getpid ());
    if (bytes >= LAIK_TCP_MINIMPI_MAX_PROCESSOR_NAME) {
        laik_tcp_errors_push (errors, __func__, 1, "Buffer to small to hold name");
        return laik_tcp_minimpi_error (errors);
    }

    *result_length = bytes;

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Initialized.html
int laik_tcp_minimpi_initialized (int* flag) {
    *flag = LAIK_TCP_MINIMPI_COMM_WORLD != NULL;

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Init.html
int laik_tcp_minimpi_init (int* argc, char*** argv) {
    (void) argc;
    (void) argv;

    // Construct a temporary error stack
    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

    // Get the configuration object
    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    // Initialize the rank variable
    size_t rank = 0;

    // Initialize the socket variable
    g_autoptr (Laik_Tcp_Socket) socket = NULL;

    // Iterate over all ranks and see if we can create a socket for one
    for (rank = 0; rank < config->addresses->len; rank++) {
        // Retrieve the address assigned to the current rank
        const char* address = g_ptr_array_index (config->addresses, rank);

        // Try to create the server socket for this address
        socket = laik_tcp_socket_new (LAIK_TCP_SOCKET_TYPE_SERVER, address, errors);

        // Check if there was an error
        if (laik_tcp_errors_present (errors)) {
            laik_tcp_debug ("Failed to bind socket to address %s", address);

            // There was an error, check if there are other addresses to try
            if (rank + 1 < config->addresses->len) {
                // There are still addresses left to try, continue
                laik_tcp_errors_clear (errors);
            } else {
                // There are no more addresses left to try, abort
                laik_tcp_errors_push (errors, __func__, 1, "Could not bind any task address");
                return laik_tcp_minimpi_error (errors);

            }
        } else {
            laik_tcp_debug ("Successfully bound socket to address %s", address);

            // There was no error, break from the loop since we found our socket
            break;
        }
    }

    // Create the flow database shared by all communicators
    flows = g_hash_table_new_full (g_bytes_hash, g_bytes_equal, laik_tcp_minimpi_destroy, g_free);

    // Create the messenger shared by all communicators
    messenger = laik_tcp_messenger_new (g_steal_pointer (&socket));

    // Create the "world" communicator
    g_autoptr (GArray) tasks = g_array_sized_new (false, false, sizeof (size_t), config->addresses->len);
    for (size_t i = 0; i < config->addresses->len; i++) {
        g_array_append_vals (tasks, &i, 1);
    }
    LAIK_TCP_MINIMPI_COMM_WORLD = laik_tcp_minimpi_new (g_steal_pointer (&tasks), rank, 0);

    // Return success
    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Recv.html
int laik_tcp_minimpi_recv (void* buffer, const int elements, Laik_Tcp_MiniMpiType datatype, const size_t sender, const int tag, const Laik_Tcp_MiniMpiComm* comm, Laik_Tcp_MiniMpiStatus* status) {
    laik_tcp_always (comm);
    laik_tcp_always (sender < comm->tasks->len);
    laik_tcp_always (sender != comm->rank);

    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

    const size_t size = elements * laik_tcp_minimpi_sizeof (datatype, errors);
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to determine size of data type");
        return laik_tcp_minimpi_error (errors);
    }

    g_autoptr (GBytes) header = laik_tcp_minimpi_header (comm->generation, TYPE_SEND_RECEIVE, sender, comm->rank, tag);
    g_autoptr (GBytes) body   = laik_tcp_messenger_get (messenger, laik_tcp_minimpi_lookup (comm, sender), header);

    if (g_bytes_get_size (body) > size) {
        laik_tcp_errors_push (errors, __func__, 1, "Message contains %zu bytes, but supplied buffer holds only %zu bytes", g_bytes_get_size (body), size);
        return laik_tcp_minimpi_error (errors);
    }

    memcpy (buffer, g_bytes_get_data (body, NULL), g_bytes_get_size (body));
    *status = g_bytes_get_size (body);

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Reduce.html
int laik_tcp_minimpi_reduce (const void* input_buffer, void* output_buffer, const int elements, const Laik_Tcp_MiniMpiType datatype, const Laik_Tcp_MiniMpiOp op, const size_t root, const Laik_Tcp_MiniMpiComm* comm) {
    laik_tcp_always (comm);
    laik_tcp_always (root < comm->tasks->len);

    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

    const size_t size = elements * laik_tcp_minimpi_sizeof (datatype, errors);
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to determine size of data type");
        return laik_tcp_minimpi_error (errors);
    }

    if (root == comm->rank) {
        // Copy the input buffer to the output buffer if necessary
        if (input_buffer != LAIK_TCP_MINIMPI_IN_PLACE) {
            memcpy (output_buffer, input_buffer, size);
        }

         // Collect the result from all other peers and laik_tcp_minimpi_combine
        for (size_t sender = 0; sender < comm->tasks->len; sender++) {
            if (sender != comm->rank) {
                g_autoptr (GBytes) header = laik_tcp_minimpi_header (comm->generation, TYPE_REDUCE, sender, root, 0);
                g_autoptr (GBytes) body   = laik_tcp_messenger_get (messenger, laik_tcp_minimpi_lookup (comm, sender), header);

                if (g_bytes_get_size (body) != size) {
                    laik_tcp_errors_push (errors, __func__, 1, "Task %zu sent %zu bytes when reducing %zu bytes", sender, g_bytes_get_size (body), size);
                    return laik_tcp_minimpi_error (errors);
                }

                laik_tcp_minimpi_combine (output_buffer, g_bytes_get_data (body, NULL), elements, datatype, op, errors);
                if (laik_tcp_errors_present (errors)) {
                    laik_tcp_errors_push (errors, __func__, 2, "Failed to reduce buffers");
                    return laik_tcp_minimpi_error (errors);
                }
            }
        }
    } else {
        const void* input = input_buffer == LAIK_TCP_MINIMPI_IN_PLACE ? output_buffer : input_buffer;

        g_autoptr (GBytes) header = laik_tcp_minimpi_header (comm->generation, TYPE_REDUCE, comm->rank, root, 0);
        g_autoptr (GBytes) body   = g_bytes_new (input, size);

        laik_tcp_messenger_push (messenger, laik_tcp_minimpi_lookup (comm, root), header, body);
    }

    return LAIK_TCP_MINIMPI_SUCCESS;
}

// https://www.mpich.org/static/docs/v3.2/www3/MPI_Send.html
int laik_tcp_minimpi_send (const void* buffer, const int elements, const Laik_Tcp_MiniMpiType datatype, const size_t receiver, const int tag, const Laik_Tcp_MiniMpiComm* comm) {
    laik_tcp_always (comm);
    laik_tcp_always (receiver < comm->tasks->len);
    laik_tcp_always (receiver != comm->rank);

    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

    const size_t size = elements * laik_tcp_minimpi_sizeof (datatype, errors);
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to determine size of data type");
        return laik_tcp_minimpi_error (errors);
    }

    g_autoptr (GBytes) header = laik_tcp_minimpi_header (comm->generation, TYPE_SEND_RECEIVE, comm->rank, receiver, tag);
    g_autoptr (GBytes) body   = g_bytes_new (buffer, size);

    laik_tcp_messenger_push (messenger, laik_tcp_minimpi_lookup (comm, receiver), header, body);

    return LAIK_TCP_MINIMPI_SUCCESS;
}

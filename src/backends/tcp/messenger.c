#include "messenger.h"
#include <glib.h>     // for g_autoptr, GBytes, g_async_queue_new_full, g_as...
#include <stdbool.h>  // for bool, true
#include <stddef.h>   // for NULL, size_t
#include <stdint.h>   // for uint64_t
#include "client.h"   // for laik_tcp_client_new, laik_tcp_client_connect
#include "config.h"   // for Laik_Tcp_Config, laik_tcp_config, Laik_Tcp_Conf...
#include "debug.h"    // for laik_tcp_debug, laik_tcp_always
#include "errors.h"   // for laik_tcp_errors_new, Laik_Tcp_Errors_autoptr
#include "map.h"      // for laik_tcp_map_get, laik_tcp_map_new, laik_tcp_ma...
#include "server.h"   // for laik_tcp_server_new, laik_tcp_server_accept
#include "socket.h"   // for laik_tcp_socket_send_bytes, laik_tcp_socket_sen...
#include "task.h"     // for Laik_Tcp_Task, laik_tcp_task_new, laik_tcp_task...
#include "time.h"     // for laik_tcp_sleep

struct Laik_Tcp_Messenger {
    Laik_Tcp_Client* client;
    Laik_Tcp_Server* server;

    Laik_Tcp_Map* inbox;
    Laik_Tcp_Map* outbox;

    GAsyncQueue* tasks;

    GThread* client_thread;
    GThread* server_thread;

    double client_seconds;
    double server_seconds;

    bool client_stop;
    bool server_stop;

    size_t add_success;
    size_t add_total;

    size_t get_success;
    size_t get_total;

    size_t try_success;
    size_t try_total;
};

typedef enum {
    MESSAGE_ADD = 0,
    MESSAGE_GET = 1,
    MESSAGE_TRY = 2,
} MessageType;

#define CHECK(exp) { if (exp) { laik_tcp_debug ("[PASS] " #exp); } else { laik_tcp_debug ("[FAIL] " #exp); continue; } }

static const char* laik_tcp_messenger_lookup (size_t id) {
    g_autoptr (Laik_Tcp_Config) config =  laik_tcp_config ();

    if (id < config->addresses->len) {
        return g_ptr_array_index (config->addresses, id);
    } else {
        return NULL;
    }
}

static void* laik_tcp_messenger_client_run (void* data) {
    laik_tcp_always (data);

    Laik_Tcp_Messenger* this = data;

    while (!this->client_stop) {
        g_autoptr (Laik_Tcp_Task)   task     = NULL;
        g_autoptr (Laik_Tcp_Socket) socket   = NULL;
        g_autoptr (GBytes)          body     = NULL;
        uint64_t                    response = 0;
        const char*                 address  = NULL;

        CHECK ((task = g_async_queue_timeout_pop (this->tasks, this->client_seconds * G_TIME_SPAN_SECOND)));

        laik_tcp_debug ("Sending a message of type %d for header 0x%08X", task->type, g_bytes_hash (task->header));

        CHECK ((address = laik_tcp_messenger_lookup (task->peer)));
        CHECK ((socket = laik_tcp_client_connect (this->client, address)));
        CHECK (laik_tcp_socket_send_uint64 (socket, task->type));
        CHECK (laik_tcp_socket_send_bytes (socket, task->header));

        switch (task->type) {
            case MESSAGE_GET:
                this->get_total++;

                CHECK (laik_tcp_socket_receive_uint64 (socket, &response));

                if (response) {
                    CHECK ((body = laik_tcp_socket_receive_bytes (socket)));
                    laik_tcp_map_add (this->inbox, task->header, body);
                    this->get_success++;
                }

                break;

            case MESSAGE_TRY:
                this->try_total++;

                CHECK ((body = laik_tcp_map_get (this->outbox, task->header, 0)));
                CHECK (laik_tcp_socket_send_bytes (socket, body));
                CHECK (laik_tcp_socket_receive_uint64 (socket, &response));

                if (response) {
                    laik_tcp_map_discard (this->outbox, task->header);
                    this->try_success++;
                }

                break;

            default:
                continue;
        }

        laik_tcp_debug ("Message of type %d for header 0x%08X was %s", task->type, g_bytes_hash (task->header), response ? "accepted" : "refused");

        // Hand the socket back so it can be re-used later on
        laik_tcp_client_store (this->client, address, g_steal_pointer (&socket));
    }

    return NULL;
}

static void* laik_tcp_messenger_server_run (void* data) {
    laik_tcp_always (data);

    Laik_Tcp_Messenger* this = data;

    while (!this->server_stop) {
        uint64_t                    type   = 0;
        g_autoptr (GBytes)          header = NULL;
        g_autoptr (GBytes)          body   = NULL;
        g_autoptr (Laik_Tcp_Socket) socket = NULL;

        CHECK ((socket = laik_tcp_server_accept (this->server, this->server_seconds)));
        CHECK (laik_tcp_socket_receive_uint64 (socket, &type));
        CHECK ((header = laik_tcp_socket_receive_bytes (socket)));

        laik_tcp_debug ("Received a message of type %d for header 0x%08X", (int) type, g_bytes_hash (header));

        switch (type) {
            case MESSAGE_ADD:
                CHECK ((body = laik_tcp_socket_receive_bytes (socket)));
                laik_tcp_map_add (this->inbox, header, body);
                break;

            case MESSAGE_GET:
                if ((body = laik_tcp_map_get (this->outbox, header, 0))) {
                    CHECK (laik_tcp_socket_send_uint64 (socket, 1));
                    CHECK (laik_tcp_socket_send_bytes (socket, body));
                    laik_tcp_map_discard (this->outbox, header);
                } else {
                    CHECK (laik_tcp_socket_send_uint64 (socket, 0));
                }
                break;

            case MESSAGE_TRY:
                CHECK ((body = laik_tcp_socket_receive_bytes (socket)));
                const bool response = laik_tcp_map_try (this->inbox, header, body);
                CHECK (laik_tcp_socket_send_uint64 (socket, response));
                break;

            default:
                continue;
        }

        // Hand the socket back so it can be re-used later on
        laik_tcp_server_store (this->server, g_steal_pointer (&socket));
    }

    return NULL;
}

void laik_tcp_messenger_free (Laik_Tcp_Messenger* this) {
    if (!this) {
        return;
    }

    laik_tcp_debug ("Terminating messenger, ADD=%zu/%zu, GET=%zu/%zu, TRY=%zu/%zu"
                   , this->add_success, this->add_total
                   , this->get_success, this->get_total
                   , this->try_success, this->try_total
                   );

    this->client_stop = true;
    this->server_stop = true;

    (void) g_thread_join (this->client_thread);
    (void) g_thread_join (this->server_thread);

    laik_tcp_client_free (this->client);
    laik_tcp_server_free (this->server);

    laik_tcp_map_free (this->inbox);
    laik_tcp_map_free (this->outbox);

    g_async_queue_unref (this->tasks);

    g_free (this);
}

GBytes* laik_tcp_messenger_get (Laik_Tcp_Messenger* this, size_t sender, GBytes* header) {
    laik_tcp_always (this);
    laik_tcp_always (header);

    GBytes* body    = NULL;
    double  seconds = 0.1;

    laik_tcp_debug ("Getting message 0x%08X from peer %zu", g_bytes_hash (header), sender);

    while (!(body = laik_tcp_map_get (this->inbox, header, seconds))) {
        laik_tcp_debug ("Waiting for message 0x%08X from peer %zu for %lf seconds timed out, queuing request"
              , g_bytes_hash (header)
              , sender
              , seconds
              );

        g_async_queue_push (this->tasks, laik_tcp_task_new (MESSAGE_GET, sender, header));

        seconds = MIN (1, seconds * 2);
    }

    laik_tcp_map_discard (this->inbox, header);

    return body;
}

Laik_Tcp_Messenger* laik_tcp_messenger_new (Laik_Tcp_Socket* socket) {
    laik_tcp_always (socket);

    Laik_Tcp_Messenger* this = g_new0 (Laik_Tcp_Messenger, 1);

    *this = (Laik_Tcp_Messenger) {
        .client = laik_tcp_client_new (3),
        .server = laik_tcp_server_new (socket, 4),

        .inbox   = laik_tcp_map_new (1<<24),
        .outbox  = laik_tcp_map_new (1<<24),

        .tasks = g_async_queue_new_full (laik_tcp_task_destroy),

        .client_seconds = 1E-2,
        .server_seconds = 1E-2,
    };

    this->client_thread = g_thread_new ("Client Thread", laik_tcp_messenger_client_run, this);
    this->server_thread = g_thread_new ("Server Thread", laik_tcp_messenger_server_run, this);

    return this;
}

void laik_tcp_messenger_push (Laik_Tcp_Messenger* this, size_t receiver, GBytes* header, GBytes* body) {
    laik_tcp_always (this);
    laik_tcp_always (header);
    laik_tcp_always (body);

    laik_tcp_debug ("Pushing message 0x%08X to peer %zu", g_bytes_hash (header), receiver);

    laik_tcp_map_add (this->outbox, header, body);

    g_async_queue_push (this->tasks, laik_tcp_task_new (MESSAGE_TRY, receiver, header));

    laik_tcp_map_block (this->outbox);
}

void laik_tcp_messenger_send (Laik_Tcp_Messenger* this, size_t receiver, GBytes* header, GBytes* body) {
    laik_tcp_always (this);
    laik_tcp_always (header);
    laik_tcp_always (body);

    const char*      address = NULL;
    Laik_Tcp_Socket* socket  = NULL;

    laik_tcp_debug ("Sending message 0x%08X to peer %zu", g_bytes_hash (header), receiver);

    while (true) {
        g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

        this->add_total++;

        CHECK ((address = laik_tcp_messenger_lookup (receiver)));
        CHECK ((socket = laik_tcp_socket_new (LAIK_TCP_SOCKET_TYPE_CLIENT, address, errors)));
        CHECK (laik_tcp_socket_send_uint64 (socket, MESSAGE_ADD));
        CHECK (laik_tcp_socket_send_bytes (socket, header));
        CHECK (laik_tcp_socket_send_bytes (socket, body));

        this->add_success++;

        break;
    }
}

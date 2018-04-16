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

#include "messenger.h"
#include <glib.h>     // for g_bytes_hash, g_autoptr, GBytes, GBytes_autoptr
#include <stdbool.h>  // for false, bool, true
#include <stddef.h>   // for NULL, size_t
#include <stdint.h>   // for uint64_t
#include "client.h"   // for laik_tcp_client_connect, laik_tcp_client_push
#include "config.h"   // for laik_tcp_config, Laik_Tcp_Config, Laik_Tcp_Conf...
#include "debug.h"    // for laik_tcp_debug, laik_tcp_always
#include "errors.h"   // for Laik_Tcp_Errors
#include "map.h"      // for laik_tcp_map_discard, laik_tcp_map_get, laik_tc...
#include "server.h"   // for laik_tcp_server_free, laik_tcp_server_new, Laik...
#include "socket.h"   // for laik_tcp_socket_send_uint64, laik_tcp_socket_se...
#include "task.h"     // for Laik_Tcp_Task, laik_tcp_task_new, Laik_Tcp_Task...
#include "time.h"     // for laik_tcp_sleep

struct Laik_Tcp_Messenger {
    Laik_Tcp_Client* client;
    Laik_Tcp_Server* server;
    Laik_Tcp_Map*    inbox;
    Laik_Tcp_Map*    outbox;
};

typedef enum {
    MESSAGE_ADD = 0,
    MESSAGE_GET = 1,
    MESSAGE_TRY = 2,
} MessageType;

#define CHECK(exp) {\
    if (exp) { \
        laik_tcp_debug ("[PASS] " #exp); \
    } else { \
        laik_tcp_debug ("[FAIL] " #exp); \
        return false; \
    } \
}

__attribute__ ((warn_unused_result))
static bool laik_tcp_messenger_client (Laik_Tcp_Messenger* this, Laik_Tcp_Task* job) {
    laik_tcp_always (this);
    laik_tcp_always (job);

    g_autoptr (GBytes)          body     = NULL;
    g_autoptr (Laik_Tcp_Socket) socket   = NULL;
    g_autoptr (Laik_Tcp_Task)   task     = job;
    uint64_t                    response = 0;

    laik_tcp_debug ("Sending a message of type %d for header 0x%08X", task->type, g_bytes_hash (task->header));

    switch (task->type) {
        case MESSAGE_ADD:
            CHECK ((body = laik_tcp_map_get (this->outbox, task->header, 0)));
            CHECK ((socket = laik_tcp_client_connect (this->client, task->peer)));
            CHECK (laik_tcp_socket_send_uint64 (socket, task->type));
            CHECK (laik_tcp_socket_send_bytes (socket, task->header));
            CHECK (laik_tcp_socket_send_bytes (socket, body));
            CHECK (laik_tcp_socket_receive_uint64 (socket, &response));
            CHECK (response);
            laik_tcp_map_discard (this->outbox, task->header);
            break;

        case MESSAGE_GET:
            CHECK ((socket = laik_tcp_client_connect (this->client, task->peer)));
            CHECK (laik_tcp_socket_send_uint64 (socket, task->type));
            CHECK (laik_tcp_socket_send_bytes (socket, task->header));
            CHECK (laik_tcp_socket_receive_uint64 (socket, &response));
            if (response) {
                CHECK ((body = laik_tcp_socket_receive_bytes (socket)));
                laik_tcp_map_add (this->inbox, task->header, body);
            }
            break;

        case MESSAGE_TRY:
            CHECK ((body = laik_tcp_map_get (this->outbox, task->header, 0)));
            CHECK ((socket = laik_tcp_client_connect (this->client, task->peer)));
            CHECK (laik_tcp_socket_send_uint64 (socket, task->type));
            CHECK (laik_tcp_socket_send_bytes (socket, task->header));
            CHECK (laik_tcp_socket_send_bytes (socket, body));
            CHECK (laik_tcp_socket_receive_uint64 (socket, &response));
            if (response) {
                laik_tcp_map_discard (this->outbox, task->header);
            }
            break;

        default:
            return false;
    }

    laik_tcp_debug ("Message of type %d for header 0x%08X was %s", task->type, g_bytes_hash (task->header), response ? "accepted" : "refused");

    // Hand the socket back so it can be re-used later on
    laik_tcp_client_store (this->client, task->peer, g_steal_pointer (&socket));

    return true;
}

static void laik_tcp_messenger_client_proxy (void* data, void* userdata) {
    laik_tcp_always (data);
    laik_tcp_always (userdata);

    Laik_Tcp_Messenger* this = userdata;
    Laik_Tcp_Task*      task = data;

    __attribute__ ((unused)) bool result = laik_tcp_messenger_client (this, task);
}

__attribute__ ((warn_unused_result))
static bool laik_tcp_messenger_server (Laik_Tcp_Messenger* this, Laik_Tcp_Socket* socket) {
    laik_tcp_always (this);
    laik_tcp_always (socket);

    g_autoptr (GBytes) body   = NULL;
    g_autoptr (GBytes) header = NULL;
    uint64_t           type   = 0;

    CHECK (laik_tcp_socket_receive_uint64 (socket, &type));
    CHECK ((header = laik_tcp_socket_receive_bytes (socket)));

    laik_tcp_debug ("Received a message of type %d for header 0x%08X", (int) type, g_bytes_hash (header));

    switch (type) {
        case MESSAGE_ADD:
            CHECK ((body = laik_tcp_socket_receive_bytes (socket)));
            laik_tcp_map_add (this->inbox, header, body);
            CHECK (laik_tcp_socket_send_uint64 (socket, 1));
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
            return false;
    }

    return true;
}

static bool laik_tcp_messenger_server_proxy (Laik_Tcp_Socket* socket, void* userdata) {
    laik_tcp_always (socket);
    laik_tcp_always (userdata);

    Laik_Tcp_Messenger* this = userdata;

    return laik_tcp_messenger_server (this, socket);
}

void laik_tcp_messenger_free (Laik_Tcp_Messenger* this) {
    if (!this) {
        return;
    }

    // Shutdown the client
    laik_tcp_client_free (this->client);

    // Shutdown the server
    laik_tcp_server_free (this->server);

    // Free the message stores
    laik_tcp_map_free (this->inbox);
    laik_tcp_map_free (this->outbox);

    // Free ourselves
    g_free (this);
}

GBytes* laik_tcp_messenger_get (Laik_Tcp_Messenger* this, size_t sender, GBytes* header, Laik_Tcp_Errors* errors) {
    laik_tcp_always (this);
    laik_tcp_always (header);

    laik_tcp_debug ("Getting message 0x%08X from peer %zu", g_bytes_hash (header), sender);

    // Get the configuration
    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    // Attempt to receive the message
    for (size_t attempt = 0; attempt < config->receive_attempts; attempt++) {
        laik_tcp_debug ("Starting attempt #%zu to receive message 0x%08X from peer %zu", attempt, g_bytes_hash (header), sender);

        // Wait for the message
        const double timeout = attempt == 0 ? config->receive_timeout : config->receive_delay;
        g_autoptr (GBytes) body = laik_tcp_map_get (this->inbox, header, timeout);

        // Check if we got the message
        if (body) {
            // Success, remove the message from the inbox and return it
            laik_tcp_map_discard (this->inbox, header);
            return g_steal_pointer (&body);
        } else {
            // Failure, queue a GET for the message
            laik_tcp_client_push (this->client, laik_tcp_task_new (MESSAGE_GET, sender, header));
        }
    }

    // Maximum number of attempts exceeded, error out
    laik_tcp_errors_push (errors, __func__, 0, "Maximum number of attempts exceeded while attempting to receive message from rank %zu", sender);
    return NULL;
}

Laik_Tcp_Messenger* laik_tcp_messenger_new (Laik_Tcp_Socket* socket) {
    laik_tcp_always (socket);

    // Get the configuration
    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    // Create the object
    Laik_Tcp_Messenger* this = g_new0 (Laik_Tcp_Messenger, 1);

    // Initialize the object
    *this = (Laik_Tcp_Messenger) {
        .client = NULL,
        .server = NULL,
        .inbox  = laik_tcp_map_new (config->inbox_size),
        .outbox = laik_tcp_map_new (config->outbox_size),
    };

    // Start the client and server
    this->client = laik_tcp_client_new (laik_tcp_messenger_client_proxy, this);
    this->server = laik_tcp_server_new (socket, laik_tcp_messenger_server_proxy, this);

    // Return the object
    return this;
}

void laik_tcp_messenger_push (Laik_Tcp_Messenger* this, size_t receiver, GBytes* header, GBytes* body) {
    laik_tcp_always (this);
    laik_tcp_always (header);
    laik_tcp_always (body);

    laik_tcp_debug ("Pushing message 0x%08X to peer %zu", g_bytes_hash (header), receiver);

    // Add the message to the outbox
    laik_tcp_map_add (this->outbox, header, body);

    // Queue the message so it may be sent later on
    laik_tcp_client_push (this->client, laik_tcp_task_new (MESSAGE_TRY, receiver, header));

    // Block here while the outbox is full, to rate-limit the outgoing messages
    laik_tcp_map_block (this->outbox);
}

void laik_tcp_messenger_send (Laik_Tcp_Messenger* this, size_t receiver, GBytes* header, GBytes* body, Laik_Tcp_Errors* errors) {
    laik_tcp_always (this);
    laik_tcp_always (header);
    laik_tcp_always (body);

    laik_tcp_debug ("Sending message 0x%08X to peer %zu", g_bytes_hash (header), receiver);

    // Get the configuration
    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    // Add the message to the outbox
    laik_tcp_map_add (this->outbox, header, body);

    // Attempt to send the message
    for (size_t attempt = 0; attempt < config->send_attempts; attempt++) {
        laik_tcp_debug ("Starting attempt #%zu to send message 0x%08X to peer %zu", attempt, g_bytes_hash (header), receiver);

        // First, try to deliver the message via an ADD ourselves. If this
        // succeeds, we can discard the message and return.
        if (laik_tcp_messenger_client (this, laik_tcp_task_new (MESSAGE_ADD, receiver, header))) {
            laik_tcp_map_discard (this->outbox, header);
            return;
        }

        // Next, check if the message is still available in the outbox, since it
        // may also have been requested by the recipient with a GET and
        // subsequently discarded. If so, we can also return.
        g_autoptr (GBytes) stored = laik_tcp_map_get (this->outbox, header, 0);
        if (!stored) {
            return;
        }

        // Finally, if nothing worked, go to sleep for some time and try again.
        laik_tcp_sleep (config->send_delay);
    }

    // Maximum number of attempts exceeded, error out
    laik_tcp_errors_push (errors, __func__, 0, "Maximum number of attempts exceeded while attempting to synchronously send message to rank %zu", receiver);
}

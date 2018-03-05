#pragma once

#include <glib.h>    // for GBytes, G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stddef.h>  // for size_t
#include "socket.h"  // for Laik_Tcp_Socket

typedef struct Laik_Tcp_Messenger Laik_Tcp_Messenger;

void laik_tcp_messenger_free (Laik_Tcp_Messenger* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Messenger, laik_tcp_messenger_free)

__attribute__ ((warn_unused_result))
GBytes* laik_tcp_messenger_get (Laik_Tcp_Messenger* this, size_t sender, GBytes* header);

__attribute__ ((warn_unused_result))
Laik_Tcp_Messenger* laik_tcp_messenger_new (Laik_Tcp_Socket* socket);

void laik_tcp_messenger_push (Laik_Tcp_Messenger* this, size_t receiver, GBytes* header, GBytes* body);

void laik_tcp_messenger_send (Laik_Tcp_Messenger* this, size_t receiver, GBytes* header, GBytes* body);

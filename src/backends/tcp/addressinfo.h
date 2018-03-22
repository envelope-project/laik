#pragma once

#include <glib.h>   // for G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <netdb.h>  // for freeaddrinfo, addrinfo

typedef struct addrinfo Laik_Tcp_AddressInfo;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_AddressInfo, freeaddrinfo)

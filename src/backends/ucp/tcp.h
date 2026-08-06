//*********************************************************************************
#pragma once

//*********************************************************************************
#include "laik-internal.h"
#include "backend-ucp-types.h"

#include <ucp/api/ucp.h>

//*********************************************************************************
#define HOME_PORT_STR "7777"
#define HOME_PORT 7777

//*********************************************************************************
/*
    Master is determined and initial peer addresses are broadcasted, enabling ucp connections for further communication
*/
void tcp_initialize_setup_connection(char *home_host, const int home_port, InstData *d);

//*********************************************************************************
/*
    Master polls new connections and broadcasts newcomer addresses
*/
size_t tcp_add_new_peers(InstData *d, Laik_Instance *instance);

//*********************************************************************************
/*
    This function is called if the current process is started during a resize event
    The process receives the newcomer addresses here
*/
size_t tcp_initialize_new_peers(InstData *d);

//*********************************************************************************
void tcp_close_connections(InstData* d);

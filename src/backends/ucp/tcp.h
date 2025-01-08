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
static int socket_fd;
// only used in main process
static int *fds;

//*********************************************************************************
void initialize_setup_connection(char *home_host, const int home_port, InstData *d);
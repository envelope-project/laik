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
void initialize_setup_connection(char *home_host, const int home_port, InstData *d);

size_t add_new_peers(InstData *d, Laik_Instance *instance);

size_t initialize_new_peers(InstData *d);
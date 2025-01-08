//*********************************************************************************
#pragma once

//*********************************************************************************
#include <ucp/api/ucp.h>

//*********************************************************************************
typedef struct _Peer
{
    size_t addrlen;
    ucp_address_t *address;
} Peer;

//*********************************************************************************
typedef struct _InstData
{
    char host[64];      // my hostname
    char location[128]; // my location
    int mylid;
    int world_size;
    int phase;
    int epoch;
    size_t addrlen;         // local address length
    ucp_address_t *address; // local address, handled by the ucp worker
    Peer *peer;
} InstData;

//*********************************************************************************
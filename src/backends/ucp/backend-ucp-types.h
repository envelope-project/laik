//*********************************************************************************
#pragma once

//*********************************************************************************
#include <ucp/api/ucp.h>

//*********************************************************************************
// Initialized as NEW
typedef enum _State
{
    NEW = 1,   // process is new to group
    INHERITED, // process is still active in new group
    INREMOVE1, // process is marked to be removed
    INREMOVE2, // process is is no longer in laik group
    DEAD       // process is no longer used
} State;

//*********************************************************************************
typedef struct _Peer
{
    State state;
    size_t addrlen;
    ucp_address_t *address;
} Peer;

//*********************************************************************************
// Global struct used to describe each processes' current state
typedef struct _InstData
{
    State state;
    int number_dead;
    char host[64];          // my hostname
    char location[128];     // my location
    int mylid;              // location id
    int world_size;         // total number of location ids/ peers (can only grow)
    int phase;              // current pohase
    int epoch;              // current epoch
    size_t addrlen;         // local ucx address length
    ucp_address_t *address; // local ucx address, memory is handled by the ucp worker
    Peer *peer;
} InstData;

//*********************************************************************************

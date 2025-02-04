//*********************************************************************************
#pragma once

//*********************************************************************************
#include <ucp/api/ucp.h>

#include "laik-internal.h"

//*********************************************************************************
typedef struct _RemoteKey
{
    ucp_rkey_h rkey_handler; // initialized by sender during unpack
    ucp_mem_h mem_handler;   // initialized by receiver during pack
    uint64_t buffer_address; // remote address
    size_t buffer_size;      // size of rdma memory region
    size_t rkey_buffer_size; // packed remote key buffer size
    size_t lid;              // location id of registered endpoint
    void *rkey_buffer;       // packed remote key buffer
} RemoteKey;

//*********************************************************************************
// receiver inserts new rdma address here
// if address is already mapped to rdma, existing remote key will be returned instead
RemoteKey *insert_new_rkey(uint64_t new_buffer_address, size_t size, ucp_context_h ucp_context);

//*********************************************************************************
// sender checks if rkey is already registered
// we have to distinguish between the different endpoints which is done using the receriver lid
ucp_rkey_h get_rkey_handle(RemoteKey *remote_key, int lid, ucp_ep_h endpoint);

//*********************************************************************************
void destroy_rkeys(ucp_context_h ucp_context);

//*********************************************************************************
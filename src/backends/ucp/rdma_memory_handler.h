//*********************************************************************************
#pragma once

//*********************************************************************************
#include <ucp/api/ucp.h>

#include "laik-internal.h"

//*********************************************************************************
// base address of local laik rdma memory segment


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
    size_t as_id;
} RemoteKey;

//*********************************************************************************
void init_rdma_memory_handler(ucp_context_h ucp_context_backend, ucp_worker_h ucp_worker_backend);

//*********************************************************************************
// receiver inserts new rdma address here
// if address is already mapped to rdma, existing remote key will be returned instead
RemoteKey *insert_new_rkey(uint64_t new_buffer_address, size_t size, ucp_context_h ucp_context);

//*********************************************************************************
RemoteKey* get_remote_key(RemoteKey *remote_key, int lid, ucp_ep_h endpoint);

//*********************************************************************************
void destroy_rkeys(ucp_context_h ucp_context, size_t as_id, bool finalize);

//*********************************************************************************
void *ucp_rdma_malloc(Laik_Data *d, size_t size);

//*********************************************************************************
void *ucp_rdma_realloc(Laik_Data *d, void* ptr, size_t size);

//*********************************************************************************
void ucp_rdma_free(Laik_Data *d, void* ptr);

//*********************************************************************************
void ucp_unmap_temporay_rdma_buffers(Laik_ActionSeq *as);

//*********************************************************************************
void ucp_map_temporay_rdma_buffers(Laik_ActionSeq *as);
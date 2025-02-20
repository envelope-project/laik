//*********************************************************************************
#include "rdma_memory_handler.h"
#include "assert.h"
#include <unistd.h>
#include <memory.h>

//*********************************************************************************
/// TODO: regulate upper limit dynamically
#define MAX_NUMBER_RKEYS 1024

//*********************************************************************************
// ucp backend context initialized during ucp_init
ucp_context_h ucp_context;
ucp_worker_h ucp_worker;

// c ensures zero initialization
/// TODO: replace simple array with scalable and performant structur which enables reductions for better memory usage
RemoteKey recv_key_list[MAX_NUMBER_RKEYS];
RemoteKey send_key_list[MAX_NUMBER_RKEYS];
int number_entries_recv_keys;
int number_entries_send_keys;;

//*********************************************************************************
void init_rdma_memory_handler(ucp_context_h ucp_context_backend, ucp_worker_h ucp_worker_backend)
{
    laik_log(LAIK_LL_Debug, "Initialized rdma memory handler");
    ucp_context = ucp_context_backend;
    ucp_worker = ucp_worker_backend;
}

//*********************************************************************************
RemoteKey* get_rkey_of_address(uint64_t addr, size_t size)
{
    for (int i = 0; i < number_entries_recv_keys; i++)
    {
        // heap addresses are growing, address must be bigger than the base address
        if (addr >= recv_key_list[i].buffer_address)
        {
            size_t max_addr = recv_key_list[i].buffer_address + recv_key_list[i].buffer_size;

            assert(max_addr > recv_key_list[i].buffer_address);
            // make sure the entire section fits into the rdma memory
            if (addr < max_addr && addr + size <= max_addr)
            {
                return &recv_key_list[i];
            }
        }
    }

    return NULL;
}

//*********************************************************************************
RemoteKey *insert_new_rkey(uint64_t new_base_address, size_t size, ucp_context_h ucp_context)
{
    RemoteKey* remote_key = get_rkey_of_address(new_base_address, size);

    if (!remote_key)
    {
        laik_log(LAIK_LL_Error, "Creating new remote key for temporary buffer [%p] with size [%lu]", (void *)new_base_address, size);
        // temporary buffer needs to be mapped to rdma as well
        assert(number_entries_recv_keys < MAX_NUMBER_RKEYS);

        recv_key_list[number_entries_recv_keys].buffer_address = new_base_address;
        recv_key_list[number_entries_recv_keys].buffer_size = size;

        ucp_mem_map_params_t mem_map_params = {
            .field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS |
                        UCP_MEM_MAP_PARAM_FIELD_LENGTH |
                        UCP_MEM_MAP_PARAM_FIELD_FLAGS,
            .address = (void *)new_base_address,
            .length = size,
            // we want to prepare as much as possible before executing the sequence
            .flags = UCP_MEM_MAP_NONBLOCK
            };

        ucs_status_t status = ucp_mem_map(ucp_context, &mem_map_params, &recv_key_list[number_entries_recv_keys].mem_handler);

        if (status != UCS_OK)
        {
            laik_panic("Could not map memory region for rdma.");
        }

        status = ucp_rkey_pack(ucp_context, recv_key_list[number_entries_recv_keys].mem_handler, &(recv_key_list[number_entries_recv_keys].rkey_buffer), &recv_key_list[number_entries_recv_keys].rkey_buffer_size);
        if (status != UCS_OK)
        {
            laik_panic("Could not pack rkey for serialization.");
        }

        // increase the size after returning the value
        return &(recv_key_list[number_entries_recv_keys++]);
    }
    
    laik_log(LAIK_LL_Debug, "Receiving: Address [%p] with size [%lu] is within buffer [%p] and size [%lu]", (void*)new_base_address, size, (void*)remote_key->buffer_address, remote_key->buffer_size);
    return remote_key;
}

//*********************************************************************************
RemoteKey* get_rkey_handle(RemoteKey *remote_key, int lid, ucp_ep_h endpoint)
{
    RemoteKey* rk = NULL;

    for (int i = 0; i < MAX_NUMBER_RKEYS; i++)
    {
        if (remote_key->buffer_address == send_key_list[i].buffer_address && remote_key->lid == send_key_list[i].lid)
        {
            rk = &send_key_list[i];
            break;
        }
    }

    if (!rk)
    {
        assert(number_entries_send_keys < MAX_NUMBER_RKEYS);

        rk = &send_key_list[number_entries_send_keys];

        rk->buffer_address = remote_key->buffer_address;
        rk->buffer_size = remote_key->buffer_size;
        rk->rkey_buffer_size = remote_key->rkey_buffer_size;
        rk->rkey_buffer = remote_key->rkey_buffer;
        rk->lid = lid;

        // is not initialized yet
        ucs_status_t status = ucp_ep_rkey_unpack(endpoint, remote_key->rkey_buffer, &(rk->rkey_handler));
        if (status != UCS_OK)
        {
            laik_panic("Could not unpack remot key");
        }

        assert(rk->rkey_handler != NULL);
        laik_log(LAIK_LL_Debug, "Unpacked rkey for buffer [%p] with target location [%d] and rkey handler [%p]", (void*)rk->buffer_address, lid, (void*)rk->rkey_handler);
        
        number_entries_send_keys++;
    }
    else
    {
        laik_log(LAIK_LL_Debug, "Sending: Address [%p] with size [%lu] is within buffer [%p] and size [%lu]", (void*)remote_key->buffer_address, remote_key->buffer_size, (void*)rk->buffer_address, rk->buffer_size);
    }

    return rk;
}

//*********************************************************************************
void destroy_rkeys(ucp_context_h ucp_context)
{
    for (int i = 0; i < number_entries_recv_keys; i++)
    {
        if (recv_key_list[i].buffer_address != UINT64_MAX)
        {
            ucp_mem_unmap(ucp_context, recv_key_list[i].mem_handler);
            laik_log(LAIK_LL_Error, "Unmapping temporary buffer [%p] with size [%lu]", (void*)recv_key_list[i].buffer_address, recv_key_list[i].buffer_size);
        }
    } 
    for (int i = 0; i < number_entries_send_keys; i++)
    {
        ucp_rkey_destroy(send_key_list[i].rkey_handler);
    }

    number_entries_recv_keys = 0;
    number_entries_send_keys = 0;
}

//*********************************************************************************
void init_data_rdma_region(void* ptr, size_t size)
{
    assert(number_entries_recv_keys < MAX_NUMBER_RKEYS);

    laik_log(LAIK_LL_Debug, "Creating new remote key for buffer [%p] with size [%lu]", ptr, size);
    ucp_mem_map_params_t mem_map_params = {
        .field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS |
                    UCP_MEM_MAP_PARAM_FIELD_LENGTH |
                    UCP_MEM_MAP_PARAM_FIELD_FLAGS,
        .address = ptr,
        .length = size,
        // we want to prepare as much as possible before executing the sequence
        .flags = UCP_MEM_MAP_NONBLOCK
        };

    recv_key_list[number_entries_recv_keys].buffer_address = (uint64_t) ptr;
    recv_key_list[number_entries_recv_keys].buffer_size = size;

    ucs_status_t status = ucp_mem_map(ucp_context, &mem_map_params, &recv_key_list[number_entries_recv_keys].mem_handler);
    if (status != UCS_OK)
    {
        laik_panic("Could not map memory region for rdma.");
    }
    status = ucp_rkey_pack(ucp_context, recv_key_list[number_entries_recv_keys].mem_handler, &(recv_key_list[number_entries_recv_keys].rkey_buffer), &recv_key_list[number_entries_recv_keys].rkey_buffer_size);
    if (status != UCS_OK)
    {
        laik_panic("Could not pack rkey for serialization.");
    }
    
    number_entries_recv_keys++;
}

//*********************************************************************************
void *ucp_rdma_malloc(Laik_Data *d, size_t size)
{
    void* ptr = malloc(size);
    // Ensure memory is backed before RDMA registration
    memset(ptr, 0, size);
    laik_log(LAIK_LL_Error, "Allocated memory for data [%d] with size: [%lu] at address [%p]", d->id, size, ptr);

    if (!ptr)
    {
        laik_log(LAIK_LL_Error, "Could not allocate enough memory for data [%d]: [%lu] Bytes", d->id, size);
        exit(1);
    }

    init_data_rdma_region(ptr, size);
    return ptr;
}

//*********************************************************************************
/// TODO: Finish to implement this if needed
void *ucp_rdma_realloc(Laik_Data *d, void* ptr, size_t size)
{
    ptr = realloc(ptr, size);
    laik_log(LAIK_LL_Debug, "Reallocated memory for data [%d] with size: [%lu] at address [%p]", d->id, size, ptr);

    if (!ptr)
    {
        laik_log(LAIK_LL_Error, "Could not allocate enough memory for data [%d]: [%lu] Bytes", d->id, size);
        exit(1);
    }
    exit(1);
    return ptr;
}

//*********************************************************************************
/// TODO: Finish to implement this if needed
void ucp_rdma_free(Laik_Data *d, void* ptr)
{
    laik_log(LAIK_LL_Error, "Freeing memory for data [%d] at address [%p]", d->id, ptr);
    assert(ucp_context);

    for (int i = 0; i < number_entries_recv_keys; i++)
    {
        if ((uint64_t)ptr ==  recv_key_list[i].buffer_address)
        {
            ucp_mem_unmap(ucp_context, recv_key_list[i].mem_handler);
            recv_key_list[i].buffer_address = UINT64_MAX;
            break;
        }
    } 

    free(ptr);
}

//*********************************************************************************
/// TODO: O(n^2) is slow, for now: prove of concept
void ucp_unmap_temporay_rdma_buffers(Laik_ActionSeq *as)
{
    
    for(int i = 0; i < as->bufferCount; i++) {
        if (as->bufSize[i] > 0) 
        {
            for (int k = 0; k < number_entries_recv_keys; k++)
            {
                if ((uint64_t)as->buf[i] == recv_key_list[k].buffer_address && as->bufSize[i] == recv_key_list[k].buffer_size)
                {
                    laik_log(LAIK_LL_Error, "Unmapping temporary buffer [%p] with size [%lu] from rdma", (void*)as->buf[i], as->bufSize[i]);
                    ucp_mem_unmap(ucp_context, recv_key_list[k].mem_handler);
                    recv_key_list[k].buffer_address = UINT64_MAX;
                }
            }
        }
    }
}

//*********************************************************************************
void ucp_map_temporay_rdma_buffers(Laik_ActionSeq *as)
{
    for(int i = 0; i < as->bufferCount; i++) {
        if (as->bufSize[i] > 0) 
        {
            laik_log(LAIK_LL_Error, "Mapping temporary buffer [%p] with size [%lu] for rdma", (void*)as->buf[i], as->bufSize[i]);
            (void)insert_new_rkey((uint64_t)as->buf[i], as->bufSize[i], ucp_context);
        }
    }
}

//*********************************************************************************

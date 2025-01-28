//*********************************************************************************
#include "rdma_memory_handler.h"
#include "assert.h"

//*********************************************************************************
// c ensures zero initialization
static RemoteKey recv_key_list[MAX_NUMBER_RKEYS];
static RemoteKey send_key_list[MAX_NUMBER_RKEYS];
static int number_entries_recv_keys;
static int number_entries_send_keys;

//*********************************************************************************
RemoteKey *insert_new_rkey(uint64_t new_base_address, size_t size, ucp_context_h ucp_context)
{
    for (int i = 0; i < number_entries_recv_keys; i++)
    {
        if (new_base_address == recv_key_list[i].buffer_address)
        {
            laik_log(LAIK_LL_Info, "Remote key for address [%p] already exists", (void *)new_base_address);
            assert(recv_key_list[i].buffer_size == size);
            return &recv_key_list[i];
        }
    }

    laik_log(LAIK_LL_Info, "Creating new remote key for buffer [%p]", (void *)new_base_address);
    number_entries_recv_keys++;
    assert(number_entries_recv_keys <= MAX_NUMBER_RKEYS);

    recv_key_list[number_entries_recv_keys].buffer_address = new_base_address;
    recv_key_list[number_entries_recv_keys].buffer_size = size;

    ucp_mem_h memh;
    ucp_mem_map_params_t mem_map_params = {
        .field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS |
                      UCP_MEM_MAP_PARAM_FIELD_LENGTH |
                      UCP_MEM_MAP_PARAM_FIELD_FLAGS,
        .address = (void *)new_base_address,
        .length = size,
        .flags = UCP_MEM_MAP_NONBLOCK};

    ucs_status_t status = ucp_mem_map(ucp_context, &mem_map_params, &memh);
    if (status != UCS_OK)
    {
        laik_panic("Could not map memory region for rdma.");
    }

    status = ucp_rkey_pack(ucp_context, memh, &(recv_key_list[number_entries_recv_keys].rkey_buffer), &recv_key_list[number_entries_recv_keys].rkey_buffer_size);
    if (status != UCS_OK)
    {
        laik_panic("Could not pack rkey for serialization.");
    }

    return &(recv_key_list[number_entries_recv_keys]);
}

//*********************************************************************************
bool is_new_rkey(RemoteKey *remote_key, int recv_lid)
{
    for (int i = 0; i < number_entries_send_keys; i++)
    {
        if (remote_key->buffer_address == send_key_list[i].buffer_address && (size_t)recv_lid == send_key_list[i].lid)
        {
            laik_log(LAIK_LL_Info, "Remote key for address [%p] is already registered for enpoint [%d]", (void *)remote_key->buffer_address, recv_lid);
            return false;
        }
    }

    number_entries_send_keys++;

    assert(number_entries_send_keys <= MAX_NUMBER_RKEYS);

    send_key_list[number_entries_send_keys].buffer_address = remote_key->buffer_address;
    send_key_list[number_entries_send_keys].lid = recv_lid;

    laik_log(LAIK_LL_Info, "Remote key for address [%p] was not registered for enpoint [%d]", (void *)remote_key->buffer_address, recv_lid);

    return true;
}

//*********************************************************************************
ucp_rkey_h get_rkey_handler(RemoteKey *remote_key, size_t recv_lid)
{
    for (int i = 0; i < number_entries_send_keys; i++)
    {
        if (remote_key->buffer_address == send_key_list[i].buffer_address && recv_lid == send_key_list[i].lid)
        {
            return false;
        }
    }

    return NULL;
}

//*********************************************************************************
ucp_rkey_h get_rkey_handle(RemoteKey *remote_key, int lid, ucp_ep_h endpoint)
{
    for (int i = 0; i < number_entries_send_keys; i++)
    {
        if (remote_key->buffer_address == send_key_list[i].buffer_address && (size_t)lid == send_key_list[i].lid)
        {
            return send_key_list[i].rkey_handler;
        }
    }

    number_entries_send_keys++;
    assert(number_entries_send_keys <= MAX_NUMBER_RKEYS);

    RemoteKey *rk = &send_key_list[number_entries_send_keys];

    rk->buffer_address = remote_key->buffer_address;
    rk->buffer_size = remote_key->buffer_size;
    rk->rkey_buffer_size = remote_key->rkey_buffer_size;
    rk->rkey_buffer = remote_key->rkey_buffer;
    rk->lid = lid;

    // is not initialized yet
    ucs_status_t status = ucp_ep_rkey_unpack(endpoint, &remote_key->rkey_buffer, &(rk->rkey_handler));
    if (status != UCS_OK)
    {
        laik_panic("Could not unpack remot key");
    }

    laik_log(LAIK_LL_Info, "Unpacked rkey for buffer [%p] with target location [%d]", (void *)rk->buffer_address, lid);

    return rk->rkey_handler;
}
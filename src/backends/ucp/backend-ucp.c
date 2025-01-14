//*********************************************************************************
#ifdef USE_UCP

//*********************************************************************************
#include "laik-backend-ucp.h"
#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>
#include <ucp/api/ucp.h>

#include "tcp.h"
#include "command_parser.h"
#include "backend-ucp-types.h"

//*********************************************************************************

// Used for calculating the message tags for send and recv
#define TAG_SOURCE_SHIFT 32
#define TAG_DEST_SHIFT 0

static ucs_status_t ep_status = UCS_OK;

//*********************************************************************************
struct _InstData;
struct _Peer;

struct ucx_context
{
    int completed;
};

static const char *UCX_MESSAGE_STRING = "UCX DATA MESSAGE";

static ucp_context_h ucp_context;
static ucp_worker_h ucp_worker;
static ucp_ep_h *ucp_endpoints;
// static ucp_mem_h ucp_memory_handle;

//*********************************************************************************
static Laik_Instance *instance = 0;
static InstData *d;

// forward decls, types/structs , global variables

static void laik_ucp_prepare(Laik_ActionSeq *);
static void laik_ucp_cleanup(Laik_ActionSeq *);
static void laik_ucp_exec(Laik_ActionSeq *as);
static void laik_ucp_finalize(Laik_Instance *);
static Laik_Group *laik_ucp_resize(Laik_ResizeRequests *reqs);
static void laik_ucp_finish_resize();

/* static void laik_ucp_updateGroup(Laik_Group *);
static bool laik_ucp_log_action(Laik_Action *a);
static void laik_ucp_sync(Laik_KVStore *kvs);  */

// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend_ucp = {
    .name = "UCP backend",
    .prepare = laik_ucp_prepare,
    .cleanup = laik_ucp_cleanup,
    .exec = laik_ucp_exec,
    .finalize = laik_ucp_finalize,
    .resize = laik_ucp_resize,
    .finish_resize = laik_ucp_finish_resize
    //.updateGroup = laik_ucp_updateGroup,
    //.log_action  = laik_ucp_log_action,
    //.sync        = laik_ucp_sync
};

//*********************************************************************************
static void request_init(void *request)
{
    struct ucx_context *context = (struct ucx_context *)request;

    context->completed = 0;
}

//*********************************************************************************
void initialize_instance_data(char *location, char *home_host, int world_size)
{
    d = (InstData *)malloc(sizeof(InstData));
    if (d == NULL)
    {
        laik_panic("Could not malloc heap for InstData\n");
        exit(1);
    }

    d->world_size = world_size;
    d->epoch = 0;
    d->phase = 0;
    d->mylid = -1;
    d->addrlen = 0;

    strcpy(d->location, location);
    strcpy(d->host, home_host);
}

//*********************************************************************************
void error_handler(void *, ucp_ep_h, ucs_status_t status)
{
    // filter graceful shutdowns
    if (status != UCS_OK)
    {
        laik_log(LAIK_LL_Error, "Rank[%d]: Endpoint is in an invalid state (%s)\n", d->mylid, ucs_status_string(ep_status));
    }
}

//*********************************************************************************
// N many endpoints required
// endpoint[n] is the endpoint between this process and the process with lid n
void initialize_endpoints(void)
{
    // array of endpoint handler
    ucp_endpoints = (ucp_ep_h *)malloc((d->world_size) * sizeof(ucp_ep_h));
    if (ucp_endpoints == NULL)
    {
        laik_panic("Could not allocate heap for endpoint handler array\n");
    }
    memset(ucp_endpoints, 0, sizeof((d->world_size) * sizeof(ucp_ep_h *)));

    for (int i = 0; i < d->world_size; i++)
    {
        ucp_ep_params_t ep_params;

        // Initialize endpoint parameters
        memset(&ep_params, 0, sizeof(ep_params));
        ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS |
                               UCP_EP_PARAM_FIELD_ERR_HANDLER;
        ep_params.address = d->peer[i].address;
        ep_params.err_handler.cb = error_handler; // Error callback
        // Create the endpoint
        ucs_status_t status = ucp_ep_create(ucp_worker, &ep_params, &ucp_endpoints[i]);

        if (status != UCS_OK)
        {
            char message[256];
            snprintf(message, sizeof(message), "Rank [%d] => Rank[%d]: Endpoint creation failed. %s\n",
                     d->mylid, i, ucs_status_string(status));
            laik_panic((const char *)message);
        }

        laik_log(1, "Rank[%d] => Rank[%d]: UCP endpoint created successfully.\n", d->mylid, i);
    }
};

//*********************************************************************************
void update_endpoints(int number_new_connections)
{
    int old_world_size = d->world_size - number_new_connections;
    ucp_endpoints = (ucp_ep_h *)realloc(ucp_endpoints, (d->world_size) * sizeof(ucp_ep_h));

    if (ucp_endpoints == NULL)
    {
        laik_panic("Not enough memory for ucp_endpoints\n");
    }

    for (int i = old_world_size; i < d->world_size; i++)
    {
        ucp_ep_params_t ep_params;
        // Initialize endpoint parameters
        memset(&ep_params, 0, sizeof(ep_params));
        ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS |
                               UCP_EP_PARAM_FIELD_ERR_HANDLER;
        ep_params.address = d->peer[i].address;
        ep_params.err_handler.cb = error_handler; // Error callback
        // Create the endpoint
        ucs_status_t status = ucp_ep_create(ucp_worker, &ep_params, &ucp_endpoints[i]);
        if (status != UCS_OK)
        {
            char message[256];
            snprintf(message, sizeof(message), "Rank [%d] => Rank[%d]: Endpoint creation failed. %s\n",
                     d->mylid, i, ucs_status_string(status));
            laik_panic((const char *)message);
        }

        laik_log(1, "Rank[%d] => Rank[%d]: UCP endpoint created successfully.\n", d->mylid, i);
    }
}

//*********************************************************************************
Laik_Group *create_new_laik_group(int old_world_size)
{
    // create new group as child of old group
    Laik_Group *world = instance->world;
    Laik_Group *group = laik_create_group(instance, d->world_size);
    group->parent = world;
    group->size = d->world_size;
    group->myid = d->mylid;
    instance->locations = d->world_size;
    for (int i = 0; i < old_world_size; i++)
    {
        group->locationid[i] = i;
        group->toParent[i] = i;
        group->fromParent[i] = i;
    }
    for (int i = old_world_size; i < d->world_size; i++)
    {
        group->locationid[i] = i;
        group->toParent[i] = -1;
        group->fromParent[i] = -1;
    }

    laik_log(1, "Rank [%d] set group size to [%d]\n", d->mylid, d->world_size);
    return group;
}

/// TODO: Create more subfunctions
//*********************************************************************************
Laik_Instance *laik_init_ucp(int *argc, char ***argv)
{
    char *str;

    (void)argc;
    (void)argv;

    if (instance)
        return instance;

    // my location string: "<hostname>:<pid>" (may be extended by master)
    char hostname[64];
    if (gethostname(hostname, 64) != 0)
    {
        // logging not initilized yet
        fprintf(stderr, "UCP cannot get host name");
        exit(1);
    }
    char location[128];
    sprintf(location, "%s:%d", hostname, getpid());

    // enable early logging
    laik_log_init_loc(location);
    if (laik_log_begin(1))
    {
        laik_log_append("UCP init: cmdline '%s", (*argv)[0]);
        for (int i = 1; i < *argc; i++)
            laik_log_append(" %s", (*argv)[i]);
        laik_log_flush("'\n");
    }

    // setting of home location: host/port to register with
    str = getenv("LAIK_UCP_HOST");
    char *home_host = str ? str : "localhost";
    str = getenv("LAIK_UCP_PORT");
    int home_port = str ? atoi(str) : 0;
    if (home_port == 0)
        home_port = HOME_PORT;
    str = getenv("LAIK_SIZE");
    int world_size = str ? atoi(str) : 1;
    if (world_size == 0)
        world_size = 1;

    laik_log(1, "UCP location '%s', home %s:%d\n", location, home_host, home_port);

    initialize_instance_data(location, home_host, world_size);

    // TODO: field_mask is used for optimizations

    // UCP temporary vars
    ucp_params_t ucp_params;
    ucp_worker_attr_t worker_attr;
    ucp_worker_params_t worker_params;
    ucp_config_t *config;
    ucs_status_t status;

    memset(&ucp_params, 0, sizeof(ucp_params));
    memset(&worker_attr, 0, sizeof(worker_attr));
    memset(&worker_params, 0, sizeof(worker_params));

    status = ucp_config_read(NULL, NULL, &config);
    if (status != UCS_OK)
    {
        laik_panic("Could not read config!\n");
    }

    ucp_params.field_mask = UCP_PARAM_FIELD_FEATURES |
                            UCP_PARAM_FIELD_REQUEST_SIZE |
                            UCP_PARAM_FIELD_REQUEST_INIT |
                            UCP_PARAM_FIELD_NAME;
    ucp_params.features = UCP_FEATURE_TAG;
    ucp_params.request_size = sizeof(struct ucx_context);
    ucp_params.request_init = request_init;
    ucp_params.name = "ucp backend";

    status = ucp_init(&ucp_params, config, &ucp_context);
    // for debug
    // ucp_config_print(config, stdout, NULL, UCS_CONFIG_PRINT_CONFIG);

    ucp_config_release(config);
    if (status != UCS_OK)
    {
        laik_panic("Could not init ucp!\n");
    }

    // TODO: field_mask and thread_mode can be used for optimizations
    worker_params.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;

    status = ucp_worker_create(ucp_context, &worker_params, &ucp_worker);
    if (status != UCS_OK)
    {
        laik_panic("Could not create worker!\n");
    }

    worker_attr.field_mask = UCP_WORKER_ATTR_FIELD_ADDRESS;

    status = ucp_worker_query(ucp_worker, &worker_attr);
    if (status != UCS_OK)
    {
        laik_panic("Could not query worker!\n");
    }

    // local ucp address information
    d->addrlen = worker_attr.address_length;
    d->address = worker_attr.address;

    laik_log(1, "Created worker with address length of %lu\n", worker_attr.address_length);

    tcp_initialize_setup_connection(home_host, home_port, d);

    assert(d->mylid >= 0);
    initialize_endpoints();

    instance = laik_new_instance(&laik_backend_ucp, d->world_size, d->mylid,
                                 d->epoch, d->phase, d->location, d);
    Laik_Group *group = laik_create_group(instance, d->world_size);
    group->size = d->world_size;
    group->myid = (d->phase == 0) ? d->mylid : -1;

    for (int i = 0; i < d->world_size; i++)
    {
        group->locationid[i] = i;
    }
    instance->world = group;

    // Only new processes during a resize have a phase > 0
    if (d->phase)
    {
        int number_new_connections;

        number_new_connections = tcp_initialize_new_peers(d);

        update_endpoints(number_new_connections);

        Laik_Group *new_group = create_new_laik_group(d->world_size - number_new_connections);

        laik_set_world(instance, new_group);
    }

    return instance;
}

//*********************************************************************************
static void laik_ucp_prepare(Laik_ActionSeq *as)
{
    if (laik_log_begin(1))
    {
        laik_log_append("UCP backend prepare:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    // mark as prepared by UCP backend: for UCP-specific cleanup + action logging
    as->backend = &laik_backend_ucp;

    bool changed = laik_aseq_splitTransitionExecs(as);
    laik_log_ActionSeqIfChanged(changed, as, "After splitting transition execs");
    if (as->actionCount == 0)
    {
        laik_aseq_calc_stats(as);
        return;
    }

    changed = laik_aseq_flattenPacking(as);
    laik_log_ActionSeqIfChanged(changed, as, "After flattening actions");

    changed = laik_aseq_combineActions(as);
    laik_log_ActionSeqIfChanged(changed, as, "After combining actions 1");

    changed = laik_aseq_allocBuffer(as);
    laik_log_ActionSeqIfChanged(changed, as, "After buffer allocation 1");

    changed = laik_aseq_splitReduce(as);
    laik_log_ActionSeqIfChanged(changed, as, "After splitting reduce actions");

    changed = laik_aseq_allocBuffer(as);
    laik_log_ActionSeqIfChanged(changed, as, "After buffer allocation 2");

    changed = laik_aseq_sort_rounds(as);
    laik_log_ActionSeqIfChanged(changed, as, "After sorting rounds");

    changed = laik_aseq_combineActions(as);
    laik_log_ActionSeqIfChanged(changed, as, "After combining actions 2");

    changed = laik_aseq_allocBuffer(as);
    laik_log_ActionSeqIfChanged(changed, as, "After buffer allocation 3");

    changed = laik_aseq_sort_2phases(as);
    // changed = laik_aseq_sort_rankdigits(as);
    laik_log_ActionSeqIfChanged(changed, as, "After sorting for deadlock avoidance");

    laik_aseq_freeTempSpace(as);

    laik_aseq_calc_stats(as);
}

//*********************************************************************************
static void laik_ucp_cleanup(Laik_ActionSeq *as)
{
    if (laik_log_begin(1))
    {
        laik_log_append("UCP backend cleanup:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    assert(as->backend == &laik_backend_ucp);
}

//*********************************************************************************
static ucs_status_t ucx_wait(ucp_worker_h ucp_worker, struct ucx_context *request,
                             const char *op_str, const char *data_str)
{
    ucs_status_t status;

    if (UCS_PTR_IS_ERR(request))
    {
        status = UCS_PTR_STATUS(request);
    }
    else if (UCS_PTR_IS_PTR(request))
    {
        while (!request->completed)
        {
            ucp_worker_progress(ucp_worker);
        }

        request->completed = 0;
        status = ucp_request_check_status(request);
        ucp_request_free(request);
    }
    else
    {
        status = UCS_OK;
    }

    if (status != UCS_OK)
    {
        laik_log(LAIK_LL_Error, "Rank [%d] Failed to %s %s (%s)\n", d->mylid, op_str, data_str, ucs_status_string(status));
    }
    else
    {
        // laik_log(1, "Rank [%d] Finish to %s %s\n", d->mylid, op_str, data_str);
    }

    return status;
}

//*********************************************************************************
static void send_handler(void *request, ucs_status_t, void *)
{
    struct ucx_context *context = (struct ucx_context *)request;
    // const char *str = (const char *)ctx;

    context->completed = 1;
}

//*********************************************************************************
static void recv_handler(void *request, ucs_status_t,
                         const ucp_tag_recv_info_t *, void *)
{
    struct ucx_context *context = (struct ucx_context *)request;

    context->completed = 1;
}

//*********************************************************************************
ucp_tag_t create_tag(int src_rank, int dest_rank)
{
    return ((ucp_tag_t)src_rank << TAG_SOURCE_SHIFT) |
           ((ucp_tag_t)dest_rank << TAG_DEST_SHIFT);
}

//*********************************************************************************
void laik_ucp_buf_send(int to_rank, char *buf, size_t count)
{
    laik_log(1, "Rank [%d] => [%d]: Sending message with size %lu.\n", d->mylid, to_rank, count);
    // laik_log_hexdump(2, count, &buf);

    ucp_request_param_t send_param;
    ucs_status_t status;
    ucs_status_ptr_t request;
    ucp_tag_t specific_tag = create_tag(d->mylid, to_rank);

    send_param.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK |
                              UCP_OP_ATTR_FIELD_USER_DATA;
    send_param.cb.send = send_handler;
    send_param.user_data = (void *)UCX_MESSAGE_STRING;

    request = ucp_tag_send_nbx((ucp_endpoints[to_rank]), (void *)buf, count, specific_tag,
                               &send_param);
    status = ucx_wait(ucp_worker, request, "send",
                      UCX_MESSAGE_STRING);

    if (request == NULL)
    {
        laik_log(1, "Request is NULL\n");
    }

    if (status != UCS_OK)
    {
        laik_log(LAIK_LL_Error, "Could not send message to %d\n", to_rank);
    }
}

//*********************************************************************************
void laik_ucp_buf_recv(int from_rank, char *buf, size_t count)
{
    ucp_request_param_t recv_param;
    ucs_status_ptr_t request;
    ucp_tag_recv_info_t info_tag;
    ucs_status_t status;
    ucp_tag_message_h msg_tag;
    ucp_tag_t specific_tag = create_tag(from_rank, d->mylid);
    ucp_tag_t tag_mask = (ucp_tag_t)(-1) << TAG_SOURCE_SHIFT;

    for (;;)
    {
        if (ep_status != UCS_OK)
        {
            laik_panic("receive data: EP disconnected\n");
        }
        /* Probing incoming events in non-block mode */
        msg_tag = ucp_tag_probe_nb(ucp_worker, specific_tag, tag_mask, 1, &info_tag);
        if (msg_tag != NULL)
        {
            /* Message arrived */
            break;
        }
        else if (ucp_worker_progress(ucp_worker))
        {
            /* Some events were polled; try again without going to sleep */
            continue;
        }
    }

    recv_param.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK |
                              UCP_OP_ATTR_FIELD_DATATYPE |
                              UCP_OP_ATTR_FLAG_NO_IMM_CMPL;
    recv_param.datatype = ucp_dt_make_contig(1);
    recv_param.cb.recv = recv_handler;

    request = ucp_tag_msg_recv_nbx(ucp_worker, (void *)buf, count, msg_tag,
                                   &recv_param);

    status = ucx_wait(ucp_worker, request, "receive", UCX_MESSAGE_STRING);

    if (status != UCS_OK)
    {
        laik_log(LAIK_LL_Error, "Rank [%d] <= Rank [%d] encountered error while receiving\n", d->mylid, from_rank);
    }

    laik_log(2, "Rank [%d] <= Rank [%d] received message with size %lu.\n", d->mylid, from_rank, count);
    // laik_log_hexdump(2, count, &buf);
}

//*********************************************************************************
void barrier()
{
    char buf[1] = {0};

    if (d->mylid == 0)
    {
        for (int i = 1; i < d->world_size; ++i)
        {
            laik_ucp_buf_recv(i, buf, sizeof(buf));
        }

        for (int i = 1; i < d->world_size; ++i)
        {
            laik_ucp_buf_send(i, buf, sizeof(buf));
        }
    }
    else
    {
        laik_ucp_buf_send(0, buf, sizeof(buf));
        laik_ucp_buf_recv(0, buf, sizeof(buf));
    }

    laik_log(2, "============================================ Rank [%d] leaves the barrier ============================================\n", d->mylid);
}

//*********************************************************************************
static void laik_ucp_exec(Laik_ActionSeq *as)
{
    Laik_Action *a = as->action;
    Laik_TransitionContext *tc = as->context[0];
    Laik_MappingList *fromList = tc->fromList;
    Laik_MappingList *toList = tc->toList;

    int elemsize = tc->data->elemsize;

    for (unsigned i = 0; i < as->actionCount; i++, a = nextAction(a))
    {
        Laik_BackendAction *ba = (Laik_BackendAction *)a;
        switch (a->type)
        {
        case LAIK_AT_Nop:
        {
            break;
        }
        case LAIK_AT_BufSend:
        {
            Laik_A_BufSend *aa = (Laik_A_BufSend *)a;
            laik_ucp_buf_send(aa->to_rank, aa->buf, aa->count * elemsize);
            break;
        }
        /* case LAIK_AT_RBufSend:
        {
            break;
        } */
        case LAIK_AT_BufRecv:
        {
            Laik_A_BufRecv *aa = (Laik_A_BufRecv *)a;
            laik_ucp_buf_recv(aa->from_rank, aa->buf, aa->count * elemsize);
            break;
        }
        case LAIK_AT_CopyFromBuf:
        {
            for (unsigned int i = 0; i < ba->count; i++)
                memcpy(ba->ce[i].ptr,
                       ba->fromBuf + ba->ce[i].offset,
                       ba->ce[i].bytes);
            break;
        }
        case LAIK_AT_CopyToBuf:
        {
            for (unsigned int i = 0; i < ba->count; i++)
                memcpy(ba->toBuf + ba->ce[i].offset,
                       ba->ce[i].ptr,
                       ba->ce[i].bytes);
            break;
        }
        case LAIK_AT_PackToBuf:
        {
            laik_exec_pack(ba, ba->map);
            break;
        }
        case LAIK_AT_MapPackToBuf:
        {
            assert(ba->fromMapNo < fromList->count);
            Laik_Mapping *fromMap = &(fromList->map[ba->fromMapNo]);
            assert(fromMap->base != 0);
            laik_exec_pack(ba, fromMap);
            break;
        }
        case LAIK_AT_UnpackFromBuf:
        {
            laik_exec_unpack(ba, ba->map);
            break;
        }
        case LAIK_AT_MapUnpackFromBuf:
        {
            assert(ba->toMapNo < toList->count);
            Laik_Mapping *toMap = &(toList->map[ba->toMapNo]);
            assert(toMap->base);
            laik_exec_unpack(ba, toMap);
            break;
        }
        case LAIK_AT_RBufLocalReduce:
        {
            assert(ba->bufID < ASEQ_BUFFER_MAX);
            assert(ba->dtype->reduce != 0);
            (ba->dtype->reduce)(ba->toBuf, ba->toBuf,
                                as->buf[ba->bufID] + ba->offset, ba->count, ba->redOp);
            break;
        }
        case LAIK_AT_RBufCopy:
        {
            assert(ba->bufID < ASEQ_BUFFER_MAX);
            memcpy(ba->toBuf, as->buf[ba->bufID] + ba->offset, ba->count * elemsize);
            break;
        }
        case LAIK_AT_BufCopy:
        {
            memcpy(ba->toBuf, ba->fromBuf, ba->count * elemsize);
            break;
        }
        default:
        {
            laik_log(LAIK_LL_Error, "Unrecognized action type\n");
            laik_log_begin(LAIK_LL_Error);
            laik_log_Action(a, as);
            laik_log_flush("");
            exit(1);
        }
        }
    }
}

//*********************************************************************************
void close_endpoints(void)
{
    for (int i = 0; i < d->world_size; i++)
    {
        if (i != d->mylid)
        {
            // lets endpoint finish operation
            ucs_status_ptr_t close_req = ucp_ep_close_nb(ucp_endpoints[i], UCP_EP_CLOSE_MODE_FLUSH);
            if (UCS_PTR_IS_PTR(close_req))
            {
                while (ucp_request_check_status(close_req) != UCS_OK)
                {
                    ucp_worker_progress(ucp_worker);
                }
                ucp_request_free(close_req);
            }
            else if (UCS_PTR_STATUS(close_req) != UCS_OK)
            {
                laik_log(LAIK_LL_Error, "Failed to close endpoint: %s\n", ucs_status_string(UCS_PTR_STATUS(close_req)));
            }
        }
    }
}

//*********************************************************************************
static void laik_ucp_finalize(Laik_Instance *inst)
{
    laik_log(1, "Rank [%d] is preparing to exit\n", d->mylid);
    assert(inst == instance);

    /* close(socket_fd);

    if (d->mylid == 0)
    {
        for (int i = 1; i < d->world_size; i++)
        {
            // close(fds[i]);
            free(d->peer[i].address);
        }
        free(fds);
    } */

    free(d->peer);

    close_endpoints();

    // also frees d->address
    ucp_worker_destroy(ucp_worker);

    laik_log(1, "Rank [%d] is exiting\n", d->mylid);

    free(d);
}

//*********************************************************************************
static Laik_Group *laik_ucp_resize(Laik_ResizeRequests *reqs)
{
    (void)reqs;

    // any previous resize must be finished
    assert(instance->world && (instance->world->parent == 0));

    /// TODO: DO i really need a barrier here? socket interaction should be an implicit barrier
    // for now helpful while debugging
    barrier();

    /// TODO: Implement command usage in tcp
    ResizeCommand *resize_commands = parse_resize_commands();

    if (resize_commands != NULL)
    {
        for (size_t i = 0; i < resize_commands->number_to_remove; ++i)
        {
            // remove peer can be performed using ucp
            // size_t rank = resize_commands->ranks_to_remove[i]
        }
    }

    // ucp cannot establish connections on its own
    size_t number_new_connections = tcp_add_new_peers(d, instance);

    free(resize_commands);

    if (number_new_connections)
    {
        update_endpoints(number_new_connections);
        return create_new_laik_group(d->world_size - number_new_connections);
    }

    return NULL;
}

//*********************************************************************************
static void laik_ucp_finish_resize()
{
    // a resize must have been started
    assert(instance->world && instance->world->parent);

    laik_log(1, "Rank [%d] reached fisish resize\n", d->mylid);
}

//*********************************************************************************
#endif /* USE_UCP */
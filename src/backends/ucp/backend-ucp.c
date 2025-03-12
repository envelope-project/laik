//*********************************************************************************
#ifdef USE_UCP

//*********************************************************************************
// laik libraries
#include "laik-backend-ucp.h"
#include "laik-internal.h"

// standard libraries
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

// UCP library
#include <ucp/api/ucp.h>

// backend libraries
#include "tcp.h"
#include "command_parser.h"
#include "backend-ucp-types.h"
#include "rdma_memory_handler.h"

//*********************************************************************************

// Used for calculating the message tags for send and recv, 2**32-1 different sources/destinations possible
#define TAG_SOURCE_SHIFT 32
#define TAG_DEST_SHIFT 0

static ucs_status_t ep_status = UCS_OK;

//*********************************************************************************

struct ucx_context
{
    int completed;
};

static const char *UCX_MESSAGE_STRING = "UCX DATA MESSAGE";

static ucp_context_h ucp_context;
static ucp_worker_h ucp_worker;
static ucp_ep_h *ucp_endpoints;

//*********************************************************************************
static Laik_Instance *instance = 0;
static InstData *d;

// forward decls, types/structs , global variables

static void laik_ucp_prepare(Laik_ActionSeq *);
static void laik_ucp_cleanup(Laik_ActionSeq *);
static void laik_ucp_exec(Laik_ActionSeq *as);
static void laik_ucp_finalize(Laik_Instance *);
static Laik_Group *laik_ucp_resize(Laik_ResizeRequests *reqs);
static void laik_ucp_finish_resize(void);
static bool laik_ucp_log_action(Laik_Action *a);
static Laik_Allocator *laik_ucp_allocator(void);
static void laik_ucp_sync(Laik_KVStore *kvs); 

void laik_ucp_buf_send(int to_lid, const void *buf, size_t count);
void laik_ucp_buf_recv(int from_lid, void *buf, size_t count);
/* 
static void laik_ucp_updateGroup(Laik_Group *);
 */
// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend_ucp = {
    .name = "UCP backend",
    .finalize = laik_ucp_finalize,
    .prepare = laik_ucp_prepare,
    .cleanup = laik_ucp_cleanup,
    .exec = laik_ucp_exec,
    .log_action = laik_ucp_log_action,
    .resize = laik_ucp_resize,
    .finish_resize = laik_ucp_finish_resize,
    .allocator = laik_ucp_allocator,
    //.updateGroup = laik_ucp_updateGroup,
    .sync        = laik_ucp_sync
};

//*********************************************************************************
// backend internal actions

#define LAIK_AT_UcpMapRecvAndUnpack (LAIK_AT_Backend + 50)
#define LAIK_AT_UcpMapPackAndSend (LAIK_AT_Backend + 51)

#define LAIK_AT_UcpRdmaSend (LAIK_AT_Backend + 52)
#define LAIK_AT_UcpRdmaRecv (LAIK_AT_Backend + 53)

// action structs are packed
#pragma pack(push, 1)

typedef struct _LAIK_A_UcpRdmaSend
{
    Laik_Action h;
    int to_rank;
    unsigned int count;
    RemoteKey* remote_key;
    char *buffer;
    uint64_t remote_buffer;
} LAIK_A_UcpRdmaSend;

typedef struct _LAIK_A_UcpRdmaRecv
{
    Laik_Action h;
    int from_rank;
    unsigned int count;
    RemoteKey* remote_key;
    char *buffer;
} LAIK_A_UcpRdmaRecv;

#pragma pack(pop)

//*********************************************************************************
// struct for synchronous communication
static void request_init(void *request)
{
    struct ucx_context *context = (struct ucx_context *)request;

    context->completed = 0;
}

//*********************************************************************************
void initialize_instance_data(const char *location, char *home_host, int world_size)
{
    d = (InstData *)malloc(sizeof(InstData));
    if (d == NULL)
    {
        laik_panic("Could not malloc heap for InstData\n");
        exit(1);
    }

    d->state = NEW;
    d->number_dead = 0;
    d->world_size = world_size;
    d->epoch = 0;
    d->phase = 0;
    d->mylid = -1;
    d->addrlen = 0;

    strcpy(d->location, location);
    strcpy(d->host, home_host);
}

//*********************************************************************************
void error_handler(void *user_data, ucp_ep_h endpoint, ucs_status_t status)
{
    (void) user_data;
    (void) endpoint;
    // filter graceful shutdowns
    if (status != UCS_OK)
    {
        laik_log(LAIK_LL_Error, "Rank[%d]: Endpoint is in an invalid state (%s)\n", d->mylid, ucs_status_string(status));
        exit(1);
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
        if (d->peer[i].state < INREMOVE1)
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

            laik_log(LAIK_LL_Info, "Rank[%d] => Rank[%d]: UCP endpoint created successfully.\n", d->mylid, i);
        }
    }
}

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
        if (d->peer[i].state < INREMOVE1)
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

            laik_log(LAIK_LL_Info, "Rank[%d] => Rank[%d]: UCP endpoint created successfully.\n", d->mylid, i);
        }
    }
}

//*********************************************************************************
// Called by necomers during init
void init_first_laik_group(int old_world_size, Laik_Group *world)
{
    Laik_Group *parent = laik_create_group(instance, old_world_size);

    parent->size = old_world_size;
    parent->myid = -1;  // not in parent group
    int i1 = 0, i2 = 0; // i1: index in parent, i2: new process index
    for (int lid = 0; lid < d->world_size; lid++)
    {
        // location ids can only grow
        if (lid == d->mylid)
        {
            world->myid = i2;
        }

        laik_log(LAIK_LL_Info, "Rank [%d lid [%d] in state [%d]", d->mylid, lid, d->peer[lid].state);

        switch (d->peer[lid].state)
        {
        case (NEW):
        {
            world->locationid[i2] = lid;
            world->toParent[i2] = -1; // did not exist before
            i2++;
            break;
        }
        case (INHERITED):
        {
            // both in old and new group
            parent->locationid[i1] = lid;
            world->locationid[i2] = lid;
            world->toParent[i2] = i1;
            world->fromParent[i1] = i2;
            i1++;
            i2++;
            break;
        }
        case (INREMOVE2):
        {
            laik_log(LAIK_LL_Info, "Rank [%d]: Rank [%d] does not exist in new group", d->mylid, lid);
            parent->locationid[i1] = lid;
            world->fromParent[i1] = -1; // does not exist in new group
            i1++;
            break;
        }
        case (DEAD):
        {
            break;
        }
        case (INREMOVE1):
        {
            // this state should never be reached, since the update_state() function is called directly after marking the peers
            // turning their INREMOVE1 state into the INREMOVE2 state
            __attribute__((fallthrough));
        }
        default:
            laik_log(LAIK_LL_Error, "Rank[%d] has invalid peer[%d] state <%d>", d->mylid, lid, d->peer[lid].state);
        }
    }

    assert(i1 == old_world_size);
    laik_log(LAIK_LL_Debug, "i1: %d i2: %d world size %d", i1, i2, d->world_size);

    world->size = i2;
    world->parent = parent;

    laik_log_flush("\n");
}

//*********************************************************************************
// Called by inherited ranks during resize
Laik_Group *create_new_laik_group(void)
{
    // create new group as child of old group
    Laik_Group *world = instance->world;
    Laik_Group *group = laik_create_group(instance, d->world_size);
    group->parent = world;

    int i1 = 0, i2 = 0; // i1: index in parent, i2: new process index
    for (int lid = 0; lid < d->world_size; lid++)
    {
        laik_log(LAIK_LL_Info, "Rank [%d lid [%d] in state [%d]", d->mylid, lid, d->peer[lid].state);
        switch (d->peer[lid].state)
        {
        case (NEW):
        {
            group->locationid[i2] = lid;
            group->toParent[i2] = -1; // did not exist before
            i2++;
            break;
        }
        case (INHERITED):
        {
            // both in old and new group
            group->locationid[i2] = lid;
            group->toParent[i2] = i1;
            group->fromParent[i1] = i2;
            i1++;
            i2++;
            break;
        }
        case (INREMOVE2):
        {
            laik_log(LAIK_LL_Info, "Rank [%d]: Rank [%d] does not exist in new group", d->mylid, lid);
            group->fromParent[i1] = -1; // does not exist in new group
            i1++;
            break;
        }
        case (DEAD):
        {
            break;
        }
        case (INREMOVE1):
        {
            // this state should never be reached, since the update_state() function is called directly after marking the peers
            // turning their INREMOVE1 state into the INREMOVE2 state
            __attribute__((fallthrough));
        }
        default:
            laik_log(LAIK_LL_Error, "Rank[%d] has invalid peer[%d] state <%d>", d->mylid, lid, d->peer[lid].state);
        }
    }

    group->size = i2;
    group->myid = group->fromParent[world->myid];
    instance->locations = d->world_size;

    return group;
}

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

    laik_log(LAIK_LL_Info, "UCP location '%s', home %s:%d\n", location, home_host, home_port);

    initialize_instance_data(location, home_host, world_size);

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
    ucp_params.features = UCP_FEATURE_TAG |
                          UCP_FEATURE_RMA;
    ucp_params.request_size = sizeof(struct ucx_context);
    ucp_params.request_init = request_init;
    ucp_params.name = "ucp backend";

    status = ucp_init(&ucp_params, config, &ucp_context);
    // for debug
    //ucp_config_print(config, stdout, NULL, UCS_CONFIG_PRINT_CONFIG);

    ucp_config_release(config);
    if (status != UCS_OK)
    {
        laik_panic("Could not init ucp!\n");
    }

    // promise that we only use master thread to communicate
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

    laik_log(LAIK_LL_Info, "Created worker with address length of %lu\n", worker_attr.address_length);

    tcp_initialize_setup_connection(home_host, home_port, d);

    // make sure that InstData was distributed by master
    assert(d->mylid >= 0);
    initialize_endpoints();

    // make ucp context available for rdma operations
    init_rdma_memory_handler(ucp_context, ucp_worker);

    instance = laik_new_instance(&laik_backend_ucp, d->world_size, d->mylid,
                                 d->epoch, d->phase, d->location, d);
    Laik_Group *group;

    if (d->phase == 0)
    {
        group = laik_create_group(instance, d->world_size);
        // we are part of initial processes: no parent
        // location IDs are process IDs in initial world
        group->myid = d->mylid;

        for (int i = 0; i < d->world_size; i++)
        {
            group->locationid[i] = i;
        }

        group->size = d->world_size;
        instance->world = group;
    }
    else
    {
        // Only joining processes during a resize have a phase > 0
        int number_new_connections;

        // updates d->world_size
        number_new_connections = tcp_initialize_new_peers(d);
        group = laik_create_group(instance, d->world_size);

        update_endpoints(number_new_connections);

        init_first_laik_group(d->world_size - number_new_connections, group);
        laik_set_world(instance, group);
    }

    return instance;
}

//*********************************************************************************
void aseq_add_rdma_send(Laik_ActionSeq *as, int round,
                        char *from_buf, unsigned int count, int to,
                        Laik_Group *group)
{
    LAIK_A_UcpRdmaSend *a = (LAIK_A_UcpRdmaSend *)laik_aseq_addBAction(as, round);

    a->h.type = LAIK_AT_UcpRdmaSend;
    a->buffer = from_buf;
    a->count = count;
    a->to_rank = to;

    int from_lid = laik_group_locationid(group, a->to_rank);

    RemoteKey remote_key;
    laik_ucp_buf_recv(from_lid, (char *)&remote_key.rkey_buffer_size, sizeof(size_t));

    remote_key.rkey_buffer = malloc(sizeof(remote_key.buffer_size));
    if (remote_key.rkey_buffer == NULL)
    {
        laik_log(LAIK_LL_Error, "Could not allocate heap for rkey buffer of size [%ld]", remote_key.buffer_size);
        exit(1);
    }

    laik_ucp_buf_recv(from_lid, (char*)remote_key.rkey_buffer, remote_key.rkey_buffer_size);
    laik_ucp_buf_recv(from_lid, (char*)&remote_key.buffer_address, sizeof(uint64_t));
    laik_ucp_buf_recv(from_lid, (char*)&remote_key.buffer_size, sizeof(size_t));

    uint64_t direct_address = 0;

    laik_ucp_buf_recv(from_lid, (char*)&direct_address, sizeof(direct_address));

    laik_log(LAIK_LL_Info, "Rank [%d] received remote key for rdma operation", d->mylid);

    a->remote_buffer = direct_address;
    a->remote_key = get_remote_key(&remote_key, from_lid, ucp_endpoints[from_lid]);
}

//*********************************************************************************
void aseq_add_rdma_recv(Laik_ActionSeq *as, int round,
                        char *to_buf, unsigned int count, int from,
                        Laik_Group *group)
{
    LAIK_A_UcpRdmaRecv *a = (LAIK_A_UcpRdmaRecv *)laik_aseq_addBAction(as, round);

    a->h.type = LAIK_AT_UcpRdmaRecv;
    a->buffer = to_buf;
    a->count = count;
    a->from_rank = from;

    int to_lid = laik_group_locationid(group, a->from_rank);

    a->remote_key = insert_new_rkey((uint64_t)to_buf, count, ucp_context);

    laik_ucp_buf_send(to_lid, (char *)&a->remote_key->rkey_buffer_size, sizeof(size_t));
    laik_ucp_buf_send(to_lid, (char*)a->remote_key->rkey_buffer, a->remote_key->rkey_buffer_size);
    laik_ucp_buf_send(to_lid, (char *)&a->remote_key->buffer_address, sizeof(uint64_t));
    laik_ucp_buf_send(to_lid, (char *)&a->remote_key->buffer_size, sizeof(size_t));

    laik_ucp_buf_send(to_lid, (char *)&to_buf, sizeof(uint64_t));

    laik_log(LAIK_LL_Info, "Rank [%d] sent remote key for rdma operation for target address [%p] and count [%ud]", d->mylid, (void*)to_buf, count);
}

//*********************************************************************************
bool ucp_aseq_inject_rdma_operations(Laik_ActionSeq *as)
{
    bool changed = false;

    Laik_Action *a = as->action;

    // must not have new actions, we want to start a new build
    assert(as->newActionCount == 0);

    Laik_TransitionContext *tc = (Laik_TransitionContext *) as->context[0];

    for (unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a))
    {
        bool handled = false;

        switch (a->type)
        {
            case LAIK_AT_BufSend:
            {
                Laik_A_BufSend* aa = (Laik_A_BufSend*) a;

                aseq_add_rdma_send(as, 3 * a->round + 1, aa->buf, aa->count * tc->data->elemsize, aa->to_rank, tc->transition->group);
                handled = true;
                break;
            }
            case LAIK_AT_BufRecv:
            {
                Laik_A_BufRecv* aa = (Laik_A_BufRecv*) a;
                aseq_add_rdma_recv(as, 3 * a->round + 1, aa->buf, aa->count * tc->data->elemsize, aa->from_rank, tc->transition->group);
                handled = true;
                break;
            }
            default:
            {
                break;
            }
        }

        if (!handled)
        {
            laik_aseq_add(a, as, 3 * a->round + 1);
        }
        else
        {
            changed = true;
        }
    }

    if (changed)
    {
        laik_aseq_activateNewActions(as);
    }
    else
    {
        laik_aseq_discardNewActions(as);
    }

    return changed;
}

//*********************************************************************************
void ucp_aseq_calc_stats(Laik_ActionSeq *as)
{
    unsigned int count;
    Laik_TransitionContext* tc = as->context[0];
    int current_tid = 0;
    Laik_Action* a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        assert(a->tid == current_tid); // TODO: only assumes actions from one transition
        switch(a->type) {
        case LAIK_AT_UcpRdmaSend:
            count = ((LAIK_A_UcpRdmaSend*)a)->count;
            as->msgAsyncSendCount++;
            as->elemSendCount += count / tc->data->elemsize;
            as->byteSendCount += count;
            break;
        case LAIK_AT_UcpRdmaRecv:
            count = ((LAIK_A_UcpRdmaRecv*)a)->count;
            as->msgAsyncRecvCount++;
            as->elemRecvCount += count / tc->data->elemsize;
            as->byteRecvCount += count;
            break;
        default: break;
        }
    }
}

//*********************************************************************************
static void laik_ucp_prepare(Laik_ActionSeq *as)
{
    // mark as prepared by UCP backend: for UCP-specific cleanup + action logging
    as->backend = &laik_backend_ucp;

    if (laik_log_begin(2))
    {
        laik_log_append("UCP backend prepare:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

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
    /* 
    ucp_map_temporay_rdma_buffers(as);
    changed = ucp_aseq_inject_rdma_operations(as);
    laik_log_ActionSeqIfChanged(changed, as, "After injecting rdma operations");
    */
    laik_aseq_freeTempSpace(as);

    ucp_aseq_calc_stats(as);
    laik_aseq_calc_stats(as);
}

//*********************************************************************************
static void laik_ucp_cleanup(Laik_ActionSeq *as)
{
    assert(as->backend == &laik_backend_ucp);

    if (laik_log_begin(1))
    {
        laik_log_append("UCP backend cleanup:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    ucp_unmap_temporay_rdma_buffers(as);
    destroy_rkeys(ucp_context, false);
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
        exit(1);
    }

    laik_log(1, "Rank [%d] Finish to %s %s\n", d->mylid, op_str, data_str);
    
    return status;
}

//*********************************************************************************
static void send_handler(void *request, ucs_status_t status, void *user_data)
{
    (void)user_data;

    struct ucx_context *context = (struct ucx_context *)request;
    laik_log(LAIK_LL_Info, "Send handler called with status: %s", ucs_status_string(status));

    context->completed = 1;
}

//*********************************************************************************
static void recv_handler(void *request, ucs_status_t status,
                         const ucp_tag_recv_info_t *tag, void *user_data)
{
    (void)status;
    (void)tag;
    (void)user_data;

    struct ucx_context *context = (struct ucx_context *)request;

    context->completed = 1;
}

//*********************************************************************************
ucp_tag_t create_tag(int src_lid, int dest_lid)
{
    laik_log(LAIK_LL_Debug, "Creating tag SRC LID <%d> DEST LID <%d> = <0x%lx>",
             src_lid, dest_lid,
             ((ucp_tag_t)src_lid << TAG_SOURCE_SHIFT) | ((ucp_tag_t)dest_lid << TAG_DEST_SHIFT));
    return ((ucp_tag_t)src_lid << TAG_SOURCE_SHIFT) |
           ((ucp_tag_t)dest_lid << TAG_DEST_SHIFT);
}

//*********************************************************************************
void laik_ucp_buf_send(int to_lid, const void *buf, size_t count)
{
    laik_log(LAIK_LL_Info, "Rank [%d] ==> [%d]: Sending message with size %lu.\n", d->mylid, to_lid, count);
    // laik_log_hexdump(2, count, &buf);

    ucp_request_param_t send_param;
    ucs_status_t status;
    ucs_status_ptr_t request;
    ucp_tag_t specific_tag = create_tag(d->mylid, to_lid);

    send_param.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK |
                              UCP_OP_ATTR_FIELD_USER_DATA;
    send_param.cb.send = send_handler;
    send_param.user_data = (void *)UCX_MESSAGE_STRING;
    request = ucp_tag_send_nbx((ucp_endpoints[to_lid]), (void *)buf, count, specific_tag,
                               &send_param);
    status = ucx_wait(ucp_worker, (struct ucx_context*)request, "send",
                      UCX_MESSAGE_STRING);

    if (request == NULL)
    {
        laik_log(LAIK_LL_Debug, "Request is NULL\n");
    }

    if (status != UCS_OK)
    {
        laik_log(LAIK_LL_Error, "Could not send message to %d\n", to_lid);
        exit(1);
    }
    else
    {
        laik_log(LAIK_LL_Info, "Rank [%d] ==> [%d]: Sent message with size %lu.\n", d->mylid, to_lid, count);
    }
}

//*********************************************************************************
void laik_ucp_buf_recv(int from_lid, void *buf, size_t count)
{
    laik_log(LAIK_LL_Info, "Rank [%d] <= Rank [%d] receiving message with size %lu.\n", d->mylid, from_lid, count);

    ucp_request_param_t recv_param;
    ucs_status_ptr_t request;
    ucp_tag_recv_info_t info_tag;
    ucs_status_t status;
    ucp_tag_message_h msg_tag;
    ucp_tag_t specific_tag = create_tag(from_lid, d->mylid);
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

    status = ucx_wait(ucp_worker, (struct ucx_context*) request, "receive", UCX_MESSAGE_STRING);

    if (status != UCS_OK)
    {
        laik_log(LAIK_LL_Error, "Rank [%d] <= Rank [%d] encountered error while receiving\n", d->mylid, from_lid);
        exit(1);
    }
    else
    {
        laik_log(LAIK_LL_Info, "Rank [%d] <= Rank [%d] received message with size %lu.\n", d->mylid, from_lid, count);
    }
}

//*********************************************************************************
void laik_ucp_rdma_send(int to_lid, char *buf, size_t count, uint64_t remote_buffer, RemoteKey* remote_key)
{
    // remote_buffer contains the exact address (including offset) within the rdma region
    // remote_key->buffer_address contains the base address of the rdma region targeted by remote_buffer

    ucs_status_t status;
    status = ucp_put_nbi(ucp_endpoints[to_lid], (const void*)buf, count, remote_buffer, remote_key->rkey_handler);
    
    do 
    {
        status = ucp_worker_flush(ucp_worker);
    } while(status == UCS_INPROGRESS);
    
    if (status != UCS_OK)
    {
        laik_log(LAIK_LL_Error, "Rank [%d] ucp_put_nbi failed: %s\n", d->mylid, ucs_status_string(status));
        exit(1);
    }
    
    static char ack[1];
    laik_ucp_buf_send(to_lid, ack, 1);

    laik_log(LAIK_LL_Debug, "Rank [%d] => Rank [%d]: RDMA Send to remote address [%p] and count [%lu]: Target RDMA [%p] with total size [%lu]",
        d->mylid, to_lid, (void*)remote_buffer, count, (void*)remote_key->buffer_address, remote_key->buffer_size);
}

//*********************************************************************************
void laik_ucp_rdma_receive(int from_lid, char *buf, size_t count, RemoteKey* remote_key)
{
    // acknowledgement from peer that rdma operation did finish
    static char ack[1];
    laik_ucp_buf_recv(from_lid, ack, 1);

    laik_log(LAIK_LL_Debug, "Rank [%d] <= Rank [%d]: RDMA Recv into address [%p] and count [%lu]: Target RDMA [%p] with total size [%lu]",
        d->mylid, from_lid, (void*)buf, count, (void*)remote_key->buffer_address, remote_key->buffer_size);
}

/// TODO: implementaion is very slow for a lot of ranks, possible solution 'tree' like communication
//*********************************************************************************
static inline void barrier()
{
    char buf[1] = {0};

    if (d->mylid == 0)
    {
        for (int i = 1; i < d->world_size; ++i)
        {
            if (d->peer[i].state < DEAD)
            {
                laik_ucp_buf_recv(i, buf, sizeof(buf));
            }
        }

        for (int i = 1; i < d->world_size; ++i)
        {
            if (d->peer[i].state < DEAD)
            {
                laik_ucp_buf_send(i, buf, sizeof(buf));
            }
        }
    }
    else
    {
        if (d->state < DEAD)
        {
            laik_ucp_buf_send(0, buf, sizeof(buf));
            laik_ucp_buf_recv(0, buf, sizeof(buf));
        }
    }

    laik_log(2, "============================================ Rank [%d] leaves the barrier ============================================\n", d->mylid);
}

//*********************************************************************************
static void laik_ucp_exec(Laik_ActionSeq *as)
{
    laik_log(LAIK_LL_Info, "Rank [%d] entering execute", d->mylid);
    Laik_Action *a = as->action;
    Laik_TransitionContext *tc = (Laik_TransitionContext *)as->context[0];
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
        case LAIK_AT_UcpRdmaSend:
        {
            LAIK_A_UcpRdmaSend *aa = (LAIK_A_UcpRdmaSend *)a;
            int to_lid = laik_group_locationid(tc->transition->group, aa->to_rank);
            if (to_lid != aa->to_rank)
            {
                laik_log(LAIK_LL_Info, "Rank [%d] ==> (Rank %d was mapped to LID %d group id [%d])", d->mylid, aa->to_rank, to_lid, tc->transition->group->gid);
            }
            laik_ucp_rdma_send(to_lid, aa->buffer, aa->count, aa->remote_buffer, aa->remote_key);
            break;
        }
        case LAIK_AT_UcpRdmaRecv:
        {
            LAIK_A_UcpRdmaRecv *aa = (LAIK_A_UcpRdmaRecv *)a;
            int from_lid = laik_group_locationid(tc->transition->group, aa->from_rank);
            if (from_lid != aa->from_rank)
            {
                laik_log(LAIK_LL_Info, "Rank [%d] <== (Rank %d was mapped to LID %d group id [%d])", d->mylid, aa->from_rank, from_lid, tc->transition->group->gid);
            }
            laik_ucp_rdma_receive(from_lid, aa->buffer, aa->count, aa->remote_key);
            break;
        }
        case LAIK_AT_BufSend:
        {
            Laik_A_BufSend *aa = (Laik_A_BufSend *)a;
            int to_lid = laik_group_locationid(tc->transition->group, aa->to_rank);
            if (to_lid != aa->to_rank)
            {
                laik_log(LAIK_LL_Info, "Rank [%d] ==> (Rank %d was mapped to LID %d group id [%d])", d->mylid, aa->to_rank, to_lid, tc->transition->group->gid);
            }
            size_t total_bytes = (size_t)aa->count * elemsize;
            laik_ucp_buf_send(to_lid, aa->buf, total_bytes);
            break;
        }
        case LAIK_AT_RBufSend:
        {
            Laik_A_RBufSend *aa = (Laik_A_RBufSend *)a;
            int to_lid = laik_group_locationid(tc->transition->group, aa->to_rank);
            if (to_lid != aa->to_rank)
            {
                laik_log(LAIK_LL_Info, "Rank [%d] ==> (Rank %d was mapped to LID %d group id [%d])", d->mylid, aa->to_rank, to_lid, tc->transition->group->gid);
            }
            laik_ucp_buf_send(to_lid, as->buf[aa->bufID] + aa->offset, (size_t)aa->count * elemsize);
            break;
        }
        case LAIK_AT_BufRecv:
        {
            Laik_A_BufRecv *aa = (Laik_A_BufRecv *)a;
            int from_lid = laik_group_locationid(tc->transition->group, aa->from_rank);
            if (from_lid != aa->from_rank)
            {
                laik_log(LAIK_LL_Info, "Rank [%d] <== (Rank %d was mapped to LID %d) group id [%d]", d->mylid, aa->from_rank, from_lid, tc->transition->group->gid);
            }
            size_t total_bytes = (size_t)aa->count * elemsize;
            laik_ucp_buf_recv(from_lid, aa->buf, total_bytes);
            break;
        }
        case LAIK_AT_RBufRecv:
        {
            Laik_A_RBufRecv *aa = (Laik_A_RBufRecv *)a;
            int from_lid = laik_group_locationid(tc->transition->group, aa->from_rank);
            if (from_lid != aa->from_rank)
            {
                laik_log(LAIK_LL_Info, "Rank [%d] <== (Rank %d was mapped to LID %d) group id [%d]", d->mylid, aa->from_rank, from_lid, tc->transition->group->gid);
            }
            laik_ucp_buf_recv(from_lid, as->buf[aa->bufID] + aa->offset, aa->count * elemsize);
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
        if (d->peer[i].state < DEAD)
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
                exit(1);
            }
        }
    }
}

/// TODO: finish ressource management
//*********************************************************************************
static void laik_ucp_finalize(Laik_Instance *inst)
{
    laik_log(LAIK_LL_Info, "Rank [%d] is preparing to exit\n", d->mylid);
    assert(inst == instance);


    tcp_close_connections(d);

    // This barrier ensures that all rdma operations are finsished before closing the endpoints
    //barrier();

    ucs_status_t status;
    do 
    {
        status = ucp_worker_flush(ucp_worker);
    }  while(status == UCS_INPROGRESS);

    destroy_rkeys(ucp_context, true);
    close_endpoints();

    // also frees d->address
    ucp_worker_destroy(ucp_worker);
    ucp_cleanup(ucp_context);

    laik_log(LAIK_LL_Info, "Rank [%d] is exiting\n", d->mylid);

    free(d->peer);
    free(d);
}

//*********************************************************************************
void delete_peer(Peer *peer, int lid)
{
    laik_log(LAIK_LL_Debug, "Rank [%d]: Deleting peer with Rank [%d]", d->mylid, lid);
    free(peer->address);

    d->world_size--;
    assert(d->world_size > 0);
}

/// TODO: tree communication instead of one to all
//*********************************************************************************
void mark_peers_to_be_removed(ResizeCommand *resize_command)
{
    size_t number_to_remove = 0;
    size_t *ranks_to_remove;
    int number_sends = 0;

    if (d->state < INREMOVE1)
    {
        if (d->mylid == 0)
        {
            if (resize_command == NULL)
            {
                laik_log(LAIK_LL_Info, "Rank[%d] No ranks have to be removed", d->mylid);

                for (int i = 1; i < d->world_size; i++)
                {
                    if (d->peer[i].state < INREMOVE1)
                    {
                        laik_ucp_buf_send(i, (char *)&(number_to_remove), sizeof(size_t));
                        number_sends++;
                    }
                }
            }
            else
            {
                laik_log_begin(LAIK_LL_Info);
                laik_log_append("Rank[%d] Removing ranks: ", d->mylid);
                for (size_t i = 0; i < resize_command->number_to_remove; i++)
                {
                    laik_log_append("[%lu] ", resize_command->ranks_to_remove[i]);
                }
                laik_log_flush("\n");

                for (int i = 1; i < d->world_size; i++)
                {
                    if (d->peer[i].state < INREMOVE1)
                    {
                        laik_ucp_buf_send(i, (char *)&(resize_command->number_to_remove), sizeof(size_t));
                        if (resize_command->number_to_remove > 0)
                        {
                            laik_ucp_buf_send(i, (char *)resize_command->ranks_to_remove, resize_command->number_to_remove * sizeof(size_t));
                        }
                        number_sends++;
                    }
                }

                laik_log(LAIK_LL_Debug, "Rank [%d] finished sending terminate commands to <%d> many peers", d->mylid, number_sends);

                number_to_remove = resize_command->number_to_remove;
                ranks_to_remove = resize_command->ranks_to_remove;
            }
        }
        else
        {
            laik_ucp_buf_recv(0, (char *)&number_to_remove, sizeof(size_t));

            if (number_to_remove > 0)
            {
                ranks_to_remove = (size_t*) malloc(number_to_remove * sizeof(size_t));

                if (ranks_to_remove == NULL)
                {
                    laik_panic("Could not allocate heap for ranks to remove array");
                }

                laik_ucp_buf_recv(0, (char *)ranks_to_remove, number_to_remove * sizeof(size_t));
            }
            laik_log(LAIK_LL_Debug, "Rank [%d] finished receiving terminate commands", d->mylid);
        }

        for (size_t i = 0; i < number_to_remove; i++)
        {
            // only update if information is new
            if (d->peer[ranks_to_remove[i]].state < INREMOVE1)
            {
                if ((size_t)d->mylid == ranks_to_remove[i])
                {
                    d->state = INREMOVE1;
                    laik_log(LAIK_LL_Info, "Rank [%d] is marked as dead.", d->mylid);
                }

                // mark peer
                d->peer[ranks_to_remove[i]].state = INREMOVE1;
                /// TODO: free resources here?

                d->number_dead++;
            }
        }

        if (d->mylid > 0 && number_to_remove > 0)
        {
            free(ranks_to_remove);
        }
    }
}

//*********************************************************************************
// NEW => INHERITED
// INHERITED => INHERITED
// INREMOVE1 => INREMOVE2
// INREMOVE2 => DEAD
// DEAD => DEAD
void update_peer_states(void)
{
    for (int i = 0; i < d->world_size; ++i)
    {
        switch (d->peer[i].state)
        {
        case (NEW):
        {
            d->peer[i].state = INHERITED;
            break;
        }
        case (INREMOVE1):
        {
            d->peer[i].state = INREMOVE2;
            break;
        }
        case (INREMOVE2):
        {
            d->peer[i].state = DEAD;
            break;
        }
        case (INHERITED):
        {
            __attribute__((fallthrough));
        }
        case (DEAD):
        {
            break;
        }
        default:
            laik_log(LAIK_LL_Error, "Rank [%d] has invalid peer[%d] state <%d>", d->mylid, i, d->peer[i].state);
        }
    }

    d->state = d->peer[d->mylid].state;
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

    // returns NULL if there was an error during parsing
    ResizeCommand *resize_commands = NULL;
    if (d->mylid == 0)
    {
        resize_commands = parse_resize_commands();
    }

    int old_number_of_dead_peers = d->number_dead;
    mark_peers_to_be_removed(resize_commands);
    update_peer_states();

    free_resize_commands(resize_commands);

    // ucp cannot establish connections on its own
    size_t number_new_connections = tcp_add_new_peers(d, instance);
    laik_log(LAIK_LL_Info, "Rank [%d] processed join and remove requests", d->mylid);

    if (number_new_connections)
    {
        update_endpoints(number_new_connections);
    }

    if (old_number_of_dead_peers != d->number_dead || number_new_connections)
    {
        return create_new_laik_group();
    }

    // nothing changed
    if (d->mylid == 0)
    {
        laik_log(LAIK_LL_Info, "Rank [%d] Nothing has to be done in resize", d->mylid);
    }
    return NULL;
}

//*********************************************************************************
static void laik_ucp_finish_resize(void)
{
    // a resize must have been started
    assert(instance->world && instance->world->parent);

    laik_log(LAIK_LL_Info, "Rank [%d] reached finish resize\n", d->mylid);
    /// TODO: free resources?
}

//*********************************************************************************
static bool laik_ucp_log_action(Laik_Action *a)
{
    switch (a->type)
    {
    case LAIK_AT_UcpRdmaRecv:
    {
        LAIK_A_UcpRdmaRecv *aa = (LAIK_A_UcpRdmaRecv *)a;
        laik_log_append(": rdma recv from Rank[%d] to buffer [%p] and count [%d]", aa->from_rank, aa->buffer, aa->count);
        break;
    }
    case LAIK_AT_UcpRdmaSend:
    {
        LAIK_A_UcpRdmaSend *aa = (LAIK_A_UcpRdmaSend *)a;
        laik_log_append(": rdma send to Rank[%d] from buffer [%p] and count [%d]", aa->to_rank, aa->buffer, aa->count);
        break;
    }
    default:
        return false;
        break;
    }
    return true;
}

//*********************************************************************************
static void laik_ucp_sync(Laik_KVStore *kvs)
{
    assert(kvs->inst == instance);

    Laik_Group* world = kvs->inst->world;
    int myid = world->myid;

    int count[2] = {0,0};

    if (myid > 0) 
    {
        // send to master, receive from master
        count[0] = (int) kvs->changes.offUsed;
        assert((count[0] == 0) || ((count[0] & 1) == 1)); // 0 or odd number of offsets
        count[1] = (int) kvs->changes.dataUsed;
        laik_log(LAIK_LL_Debug, "UCP sync: sending %d changes (total %d chars) to T0",
                 count[0] / 2, count[1]);
        
        laik_ucp_buf_send(0, count, sizeof(count));

        if (count[0] > 0)
        {
            assert(count[1] > 0);
            laik_ucp_buf_send(0, kvs->changes.off, count[0] * sizeof(count[0]));
            laik_ucp_buf_send(0, kvs->changes.data, count[1] * sizeof(char));
        }
        else 
        {
            assert(count[1] == 0);
        }

        laik_ucp_buf_recv(0, count, sizeof(count));
        laik_log(LAIK_LL_Debug, "MPI sync: getting %d changes (total %d chars) from T0",
                 count[0] / 2, count[1]);

        if (count[0] > 0) 
        {
            assert(count[1] > 0);
            laik_kvs_changes_ensure_size(&(kvs->changes), count[0], count[1]);
            laik_ucp_buf_recv(0, kvs->changes.off, count[0] * sizeof(count[0]));
            laik_ucp_buf_recv(0, kvs->changes.data, count[1] * sizeof(char));

            laik_kvs_changes_set_size(&(kvs->changes), count[0], count[1]);
            // TODO: opt - remove own changes from received ones
            laik_kvs_changes_apply(&(kvs->changes), kvs);
        }
        else
            assert(count[1] == 0);

        return;
    }

    // master: receive changes from all others, sort, merge, send back

    // first sort own changes, as preparation for merging
    laik_kvs_changes_sort(&(kvs->changes));

    Laik_KVS_Changes recvd, changes;
    laik_kvs_changes_init(&changes); // temporary changes struct
    laik_kvs_changes_init(&recvd);

    Laik_KVS_Changes *src, *dst, *tmp;
    // after merging, result should be in dst;
    dst = &(kvs->changes);
    src = &changes;

    for(int i = 1; i < world->size; i++) {
        if (d->peer[i].state < DEAD)
        {
            laik_ucp_buf_recv(i, count, sizeof(count));
            
            laik_log(LAIK_LL_Debug, "MPI sync: getting %d changes (total %d chars) from T%d",
                    count[0] / 2, count[1], i);
            laik_kvs_changes_set_size(&recvd, 0, 0); // fresh reuse
            laik_kvs_changes_ensure_size(&recvd, count[0], count[1]);
            if (count[0] == 0) {
                assert(count[1] == 0);
                continue;
            }

            assert(count[1] > 0);
            laik_ucp_buf_recv(i, recvd.off, count[0] * sizeof(count[0]));
            laik_ucp_buf_recv(i, recvd.data, count[1] * sizeof(char));

            laik_kvs_changes_set_size(&recvd, count[0], count[1]);

            // for merging, both inputs need to be sorted
            laik_kvs_changes_sort(&recvd);

            // swap src/dst: now merging can overwrite dst
            tmp = src; src = dst; dst = tmp;

            laik_kvs_changes_merge(dst, src, &recvd);
        }
    }

    // send merged changes to all others: may be 0 entries
    count[0] = dst->offUsed;
    count[1] = dst->dataUsed;
    assert(count[1] > count[0]); // more byte than offsets
    for(int i = 1; i < world->size; i++) {
        if (d->peer[i].state < DEAD)
        {
            laik_log(1, "MPI sync: sending %d changes (total %d chars) to T%d",
            count[0] / 2, count[1], i);
            
            laik_ucp_buf_send(i, count, sizeof(count));
            
            if (count[0] == 0) continue;
            
            laik_ucp_buf_send(i, dst->off, count[0] * sizeof(count[0]));
            laik_ucp_buf_send(i, dst->data, count[1] * sizeof(char));
        }
    }

    // TODO: opt - remove own changes from received ones
    laik_kvs_changes_apply(dst, kvs);

    laik_kvs_changes_free(&recvd);
    laik_kvs_changes_free(&changes);
}

//*********************************************************************************
static Laik_Allocator* laik_ucp_allocator(void)
{
    return laik_new_allocator(ucp_rdma_malloc, ucp_rdma_free, ucp_rdma_realloc);
}

//*********************************************************************************
#endif /* USE_UCP */
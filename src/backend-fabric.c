#ifdef USE_FABRIC
#include "laik-internal.h"
#include "laik-backend-fabric.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#define HOME_PORT_STR "7777"
#define HOME_PORT 7777
#define PANIC_NZ(a) if ((ret = a)) laik_log(LAIK_LL_Panic, #a " failed: %s", \
    fi_strerror(ret));
const int ll = LAIK_LL_Debug;

void fabric_prepare(Laik_ActionSeq *as);
void fabric_exec(Laik_ActionSeq *as);
void fabric_cleanup(Laik_ActionSeq *as);
void fabric_finalize(Laik_Instance *inst);
bool fabric_log_action(Laik_Action *a);

/* Declare the check_local() function from src/backend_tcp2.c, which we are
 * re-using because I couldn't find a better way to do it with libfabric
 */
bool check_local(char *host);

/* Backend-specific actions */
/* The LibFabric backend uses RMAs for send and receive, which complete
 * asynchronously. This makes it necessary to wait for the completion of the
 * RMAs before proceeding to the next round. */
#define LAIK_AT_FabRmaWait	(LAIK_AT_Backend + 0)
#pragma pack(push,1)
typedef struct {
  Laik_Action h;
  unsigned int count; /* How many CQ reports to wait for */
} Laik_A_FabRmaWait;
#pragma pack(pop)

bool fabric_log_action(Laik_Action *a) {
  if (a->type != LAIK_AT_FabRmaWait) return false;
  Laik_A_FabRmaWait *aa = (Laik_A_FabRmaWait*) a;
  laik_log_append("FabRmaWait: count %u", aa->count);
  return true;
}

/* TODO: Figure out what InstData is needed for */
static struct _InstData {
  int mylid;
  int world_size;
  int addrlen;
} d;
typedef struct _InstData InstData;

static struct fi_info *info;
static struct fid_fabric *fabric;
static struct fid_domain *domain;
static struct fid_ep *ep;
static struct fi_av_attr av_attr = { 0 };
static struct fi_cq_attr cq_attr = { .wait_obj = FI_WAIT_UNSPEC };
/* static struct fi_eq_attr eq_attr = { 0 }; */
static struct fid_av *av;
static struct fid_cq *cq;
/* static struct fid_eq *eq; */
static struct fid_mr **mregs = NULL;
int ret;

static Laik_Backend laik_backend_fabric = {
  .name		= "Libfabric Backend",
  .prepare	= fabric_prepare,
  .exec		= fabric_exec,
  .cleanup	= fabric_cleanup,
  .finalize	= fabric_finalize,
  .log_action	= fabric_log_action
};

Laik_Instance *laik_init_fabric(int *argc, char ***argv) {
  char *str;

  (void) argc;
  (void) argv;

  /* Init logging as "<hostname>:<pid>", like TCP2 does it
   * TODO: Does this make sense or is it better to use some fabric information?
   */
  char hostname[50];
  if (gethostname(hostname, 50)) {
    fprintf(stderr, "Libfabric: cannot get host name\n");
    exit(1);
  }
  char location[70];
  sprintf(location, "%s:%d", hostname, getpid());
  laik_log_init_loc(location);
  /* TODO: Should we log cmdline like TCP2? */

  /* Hints for fi_getinfo() */
  struct fi_info *hints = fi_allocinfo();
  /* TODO: Are these attributes OK? Do we really need FI_MSG? */
  hints->ep_attr->type = FI_EP_RDM;
  hints->caps = FI_MSG | FI_RMA;

  /* Choose the first provider that supports RMA and can reach the master node
   * TODO: How to make sure that all nodes chose the same provider?
   */
  str = getenv("LAIK_FABRIC_HOST");
  char *home_host = str ? str : "localhost";
  str = getenv("LAIK_FABRIC_PORT");
  char *home_port_str = str ? str : HOME_PORT_STR;
  int home_port = atoi(home_port_str);
  if (home_port == 0) home_port = HOME_PORT;
  str = getenv("LAIK_SIZE");
  int world_size = str ? atoi(str) : 1;
  if (world_size == 0) world_size = 1;
  ret = fi_getinfo(FI_VERSION(1,21), home_host, NULL, 0,
      hints, &info);
  if (ret || !info) laik_panic("No suitable fabric provider found!");
  laik_log(ll, "Selected fabric \"%s\", domain \"%s\"",
      info->fabric_attr->name, info->domain_attr->name);
  laik_log(ll, "Addressing format is: %d", info->addr_format);
  fi_freeinfo(hints);

  /* Set up address vector */
  PANIC_NZ(fi_fabric(info->fabric_attr, &fabric, NULL));
  PANIC_NZ(fi_domain(fabric, info, &domain, NULL));
  av_attr.type = FI_AV_TABLE;
  av_attr.count = world_size;
  PANIC_NZ(fi_av_open(domain, &av_attr, &av, NULL));

  /* Open the endpoint, bind it to an event queue and a completion queue */
  PANIC_NZ(fi_endpoint(domain, info, &ep, NULL));
  PANIC_NZ(fi_cq_open(domain, &cq_attr, &cq, NULL));
  PANIC_NZ(fi_ep_bind(ep, &av->fid, 0));
  PANIC_NZ(fi_ep_bind(ep, &cq->fid, FI_TRANSMIT|FI_RECV));
#if 0
  /* TODO: do we need an event queue for anything? */
  PANIC_NZ(fi_eq_open(fabric, &eq_attr, &eq, NULL));
  PANIC_NZ(fi_ep_bind(ep, &eq->fid, 0)); /* TODO: is 0 OK here? */
#endif
  PANIC_NZ(fi_enable(ep));

  /* Get the address of the endpoint */
  /* TODO: Don't hard-code array length, call fi_getname twice instead */
  char fi_addr[160]; 
  size_t fi_addrlen = 160;
  PANIC_NZ(fi_getname(&ep->fid, fi_addr, &fi_addrlen));
  laik_log(ll, "Got libfabric EP addr of length %zu:", fi_addrlen);
  laik_log_hexdump(ll, fi_addrlen, fi_addr);

  /* Allocate space for list of peers
   * This can't be done earlier because we only get the address size,
   * which depends on protocol, when we call fi_getname() */
  char *peers = calloc(world_size, fi_addrlen);
  if (!peers) laik_panic("Failed to alloc memory");

  /* Sync node addresses over regular sockets, using a similar process as the
   * tcp2 backend, since libfabric features aren't needed here.
   * 
   * This is also recommended in the "Starting Guide for Writing to libfabric",
   * which can be found here:
   * https://www.slideshare.net/JianxinXiong/getting-started-with-libfabric
   */

  /* Get address of home node */
  struct addrinfo sock_hints = {0}, *res;
  sock_hints.ai_family = AF_INET;
  sock_hints.ai_socktype = SOCK_STREAM;
  getaddrinfo(home_host, home_port_str, &sock_hints, &res);
  
  /* Regardless of whether we become master, we need a socket */
  int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sockfd < 0) laik_panic("Failed to create socket");

  /* If home host is localhost, try to become master */
  bool try_master = check_local(home_host);
  bool is_master = 0;
  if (try_master) {
    /* Avoid wait time to bind to same port */
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
          &(int){1}, sizeof(int)) < 0) laik_panic("Cannot set SO_REUSEADDR");
    /* Try binding a socket to the home address */
    laik_log(ll, "Trying to become master");
    is_master = (bind(sockfd, res->ai_addr, res->ai_addrlen) == 0);
  }

  /* Get address list */
  if (is_master) {
    laik_log(ll, "Became master!");
    d.mylid = 0;
    /* Create listening socket on home node */
    if (listen(sockfd, world_size)) laik_panic("Failed to listen on socket");
    /* Insert our own address into address list */
    memcpy(peers, fi_addr, fi_addrlen);
    /* Get addresses of all nodes */
    int fds[world_size-1];
    for (int i = 0; i < world_size - 1; i++) {
      laik_log(ll, "%d out of %d connected...", i, world_size - 1);
      fds[i] = accept(sockfd, NULL, NULL);
      read(fds[i], peers + (i+1) * fi_addrlen, fi_addrlen);
    }
    /* Send assigned number and address list to every non-master node */
    for (int i = 0; i < world_size - 1; i++) {
      int iplus = i + 1;
      write(fds[i], &iplus, sizeof(int));
      write(fds[i], peers, world_size * fi_addrlen);
      close(fds[i]);
    }
  } else {
    laik_log(ll, "Didn't become master!");
    laik_log(ll, "Connecting to:");
    laik_log_hexdump(ll, res->ai_addrlen, res->ai_addr);
    if (connect(sockfd, res->ai_addr, res->ai_addrlen)) {
      laik_log(LAIK_LL_Error, "Failed to connect: %s", strerror(errno));
      exit(1);
    }
    write(sockfd, fi_addr, fi_addrlen);
    read(sockfd, &(d.mylid), sizeof(int));
    read(sockfd, peers, world_size * fi_addrlen);
  }
  close(sockfd);

  ret = fi_av_insert(av, peers, world_size, NULL, 0, NULL);
  if (ret != world_size) laik_panic("Failed to insert addresses into AV");
  free(peers);

  /* Initialize LAIK */
  d.world_size = world_size;
  d.addrlen = fi_addrlen;
  Laik_Instance *inst = laik_new_instance(&laik_backend_fabric, world_size,
      d.mylid, 0, 0, "", &d); /* TODO: what is location? */
  Laik_Group *world = laik_create_group(inst, world_size);
  world->size = world_size;
  world->myid = d.mylid;
  inst->world = world;
  return inst;
}

void add_fabRmaWait(Laik_Action **next, unsigned round, unsigned count) {
  Laik_A_FabRmaWait wait = {
    .h = {
      .type  = LAIK_AT_FabRmaWait,
      .len   = sizeof(Laik_A_FabRmaWait),
      .round = round,
      .tid   = 0,
      .mark  = 0
    },
    .count = count
  };
  memcpy(*next, &wait, sizeof(Laik_A_FabRmaWait));
  *next = nextAction((*next));
}

void fabric_prepare(Laik_ActionSeq *as) {
  if (as->actionCount == 0) {
    laik_aseq_calc_stats(as);
    return;
  }

  /* Mark action seq as prepared by fabric backend, so that
   * fabric_cleanup() gets invoked for cleanup */
  as->backend = &laik_backend_fabric;

  laik_log_ActionSeqIfChanged(true, as, "Original sequence");
  bool changed = laik_aseq_splitTransitionExecs(as);
  laik_log_ActionSeqIfChanged(changed, as, "After splitting transition execs");
  changed = laik_aseq_flattenPacking(as);
  laik_log_ActionSeqIfChanged(changed, as, "After flattening actions");

  /* TODO: does laik_aseq_replaceWithAllReduce() make sense? */

  /* TODO: This is just copied from src/backend-mpi.c. Do all these
   *       transformations also make sense for libfabric, the same order? */
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
  laik_log_ActionSeqIfChanged(changed, as,
      "After sorting for deadlock avoidance");
  laik_aseq_freeTempSpace(as);

  /* TODO: Any more efficient way of storing memory registrations?
   *       How much memory does this really waste? Is it better or worse than
   *       introducing a backend-specific "register memory binding" action? */
  mregs = realloc(mregs, (as->actionCount + 1) * sizeof(struct fid_mr*));
  if (!mregs) laik_panic("Failed to alloc memory");
  int mnum = 0;

  /* Iterate over action sequence, and:
   * - Register memory for RMA
   * - Create new action sequence that adds FabRmaWait at the end of any round
   *   that performs at least one RMA */

  Laik_TransitionContext *tc = as->context[0];
  Laik_Action *a = as->action;
  int elemsize = tc->data->elemsize;

  Laik_Action *newAction =
      malloc(as->bytesUsed + as->roundCount * sizeof(Laik_A_FabRmaWait));
  if (!newAction) laik_panic("Failed to alloc memory");
  Laik_Action *nextNewA = newAction;
  unsigned waitCnt = 0;

  int rmas = 0;
  int lastRound = 1;
  for (unsigned i = 0; i < as->actionCount; i++, a = nextAction(a)) {
    /* Add FabRmaWait that waits for all RMAs of the last round to complete */
    if (a->round != lastRound) {
      if (rmas > 0) {
        add_fabRmaWait(&nextNewA, lastRound, rmas);
        waitCnt++;
      }
      rmas = 0;
      lastRound = a->round;
    }

    /* Count RMAs and copy actions into newAction */
    switch (a->type) {
      case LAIK_AT_BufSend:
        rmas++;
        break;
      case LAIK_AT_BufRecv:
        Laik_A_BufRecv *aa = (Laik_A_BufRecv*) a;
        int reserve = aa->count * elemsize;
        laik_log(ll, "Reserving %d * %d = %d bytes\n",
            aa->count, elemsize, reserve);
        PANIC_NZ(fi_mr_reg(
            domain, aa->buf, reserve*aa->count, FI_REMOTE_WRITE,
            0, 0, 0, &mregs[mnum++], NULL));
        rmas++;
        break;
    }
    memcpy(nextNewA, a, a->len);
    nextNewA = nextAction(nextNewA);
  }
  /* Add a FabRmaWait after the final round, too */
  add_fabRmaWait(&nextNewA, lastRound, rmas);
  waitCnt++;

  /* Terminate memory registration list */
  mregs[mnum] = NULL;

  /* Replace old action sequence with new and update AS information */
  free(as->action);
  as->action       = newAction;
  as->actionCount += waitCnt;
  as->bytesUsed   += waitCnt * sizeof(Laik_A_FabRmaWait);

  laik_log_ActionSeqIfChanged(true, as, "After adding FabRmaWait");

  laik_aseq_calc_stats(as);
}

void fabric_exec(Laik_ActionSeq *as) {
  Laik_Action *a = as->action;
  Laik_TransitionContext *tc = as->context[0];
  int elemsize = tc->data->elemsize;
  char cq_buf[128]; /* TODO: what is minimum required size? */
  for (unsigned i = 0; i < as->actionCount; i++, a = nextAction(a)) {
    switch (a->type) {
      case LAIK_AT_Nop: break;
      case LAIK_AT_BufRecv: {
        break;
      }
      case LAIK_AT_BufSend: {
        Laik_A_BufSend *aa = (Laik_A_BufSend*) a;
        /* TODO: Does the uint64_t data have any significance?
         *       Is it a problem that I just set it to 0?*/
        while ((ret = fi_writedata(ep, aa->buf, elemsize * aa->count, NULL,
                0, aa->to_rank, 0, 0, NULL)) == -FI_EAGAIN);
        if (ret)
          laik_log(LAIK_LL_Panic,
              "fi_writedata() failed: %s", fi_strerror(ret));
        break;
      }
      case LAIK_AT_FabRmaWait: {
        Laik_A_FabRmaWait *aa = (Laik_A_FabRmaWait*) a;
        unsigned completions = 0;
        while (completions < aa->count) {
          /* TODO: either do something with the retrieved information
           *       or replace the CQ with a counter */
          ret = fi_cq_sread(cq, cq_buf, 1, NULL, -1);
          if (ret == -EAGAIN) continue;
          assert(ret > 0); /* TODO: actual error handling */
          completions += ret;
        }
        break;
      }
      case LAIK_AT_RBufLocalReduce: {
        Laik_BackendAction *ba = (Laik_BackendAction*) a;
        assert(ba->bufID < ASEQ_BUFFER_MAX);
        assert(ba->dtype->reduce != 0);
        (ba->dtype->reduce)(ba->toBuf, ba->toBuf,
            as->buf[ba->bufID] + ba->offset, ba->count, ba->redOp);
        break;
      }
      default:
        laik_log(LAIK_LL_Error, "Unrecognized action type");
        laik_log_begin(LAIK_LL_Error);
        laik_log_Action(a, as);
        laik_log_flush("");
        exit(1);
    }
  }
}

void fabric_cleanup(Laik_ActionSeq *as) {
  (void) as;
  /* Clean up RDMAs */
  for (struct fid_mr **mr = mregs; *mr; mr++) {
    PANIC_NZ(fi_close((struct fid *) *mr));
  }
}

void fabric_finalize(Laik_Instance *inst) {
  /* TODO: Final cleanup */
  (void) inst;

  free(mregs);
  fi_close((struct fid *) ep);
  fi_close((struct fid *) domain);
  fi_close((struct fid *) fabric);
  fi_freeinfo(info);
}

#endif /* USE_FABRIC */

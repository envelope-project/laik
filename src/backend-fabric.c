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
#define RETRY(a) \
  while ((ret = a) == -FI_EAGAIN); \
  if (ret) laik_log(LAIK_LL_Panic, #a " failed: %s", fi_strerror(ret));
#define RETRYCQ(a, cq) \
  while ((ret = a) == -FI_EAGAIN); \
  if (ret < 0) handle_cq_error(#a, cq);

/* Define this to enable a lot of additional output for printf-debugging */
/*#define PFDBG 1*/
#ifdef PFDBG
#define D(a) a
#else
#define D(a) ;
#endif

const int ll = LAIK_LL_Debug;
const char *allocfail = "Failed to allocate memory";

/* Empty marker must be -1 instead of 0, because 0 is a valid key for a message
 * sent in the AS with ID 0 from node 0 to any other node. (make_key(0,0,0)) */
const uint64_t ack_empty = (uint64_t) -1;

void fabric_prepare(Laik_ActionSeq *as);
void fabric_exec(Laik_ActionSeq *as);
void fabric_cleanup(Laik_ActionSeq *as);
void fabric_finalize(Laik_Instance *inst);
bool fabric_log_action(Laik_Action *a);

/* Declare the check_local() function from src/backend_tcp2.c, which we are
 * re-using because I couldn't find a better way to do it with libfabric
 */
bool check_local(char *host);
void handle_cq_error(char *op, struct fid_cq *cq);
void ack_rma(uint64_t key);
void await_completions(struct fid_cq *cq, int num);
uint64_t make_key(int rnode, int snode, uint8_t seq);
int get_sender(uint64_t key);
void barrier(void);

/* Backend-specific actions */
/* The LibFabric backend uses RMAs for send and receive, which complete
 * asynchronously. This makes it necessary to wait for the completion of the
 * RMAs before proceeding to the next round. */
#define LAIK_AT_FabAsyncRecv	(LAIK_AT_Backend + 0)
#define LAIK_AT_FabRecvWait	(LAIK_AT_Backend + 1)
#define LAIK_AT_FabAsyncSend	(LAIK_AT_Backend + 2)
#define LAIK_AT_FabSendWait	(LAIK_AT_Backend + 3)
#pragma pack(push,1)
typedef Laik_A_BufRecv Laik_A_FabAsyncRecv;
typedef struct {
  Laik_Action h;
  unsigned int count; /* How many CQ reports to wait for */
} Laik_A_FabRecvWait;
typedef Laik_A_BufSend Laik_A_FabAsyncSend;
typedef Laik_A_FabRecvWait Laik_A_FabSendWait;
#pragma pack(pop)

bool fabric_log_action(Laik_Action *a) {
  switch (a->type) {
    case LAIK_AT_FabRecvWait: {
      Laik_A_FabRecvWait *aa = (Laik_A_FabRecvWait*) a;
      laik_log_append("FabRecvWait: count %u", aa->count);
      break;
    }
    case LAIK_AT_FabSendWait: {
      Laik_A_FabSendWait *aa = (Laik_A_FabSendWait*) a;
      laik_log_append("FabSendWait: count %u", aa->count);
      break;
    }
    case LAIK_AT_FabAsyncSend: {
      Laik_A_FabAsyncSend *aa = (Laik_A_FabAsyncSend*) a;
      laik_log_append("FabAsyncSend: from %p, count %d ==> T%d",
                      aa->buf, aa->count, aa->to_rank);
      break;
    }
    case LAIK_AT_FabAsyncRecv: {
      Laik_A_FabAsyncRecv *aa = (Laik_A_FabAsyncRecv*) a;
      laik_log_append("FabAsyncRecv: T%d ==> to %p, count %d",
                      aa->from_rank, aa->buf, aa->count);
      break;
    }
    default: {
      return false;
    }
  }
  return true;
}

/* TODO: Figure out what InstData is needed for */
static struct _InstData {
  int mylid;
  int world_size;
  int addrlen;
} d;
typedef struct _InstData InstData;

/* Global variables for libfabric */
static struct fi_info *info;
static struct fid_fabric *fabric;
static struct fid_domain *domain;
static struct fid_ep *ep;
static struct fi_av_attr av_attr = { 0 };
static struct fi_cq_attr cq_attr = {
  .wait_obj	= FI_WAIT_UNSPEC,
/* Format MUST be FI_CQ_FORMAT_DATA or a superset of it, so that remote CQ
 * data is actually awaited. That is described here:
 * https://github.com/ofiwg/libfabric/discussions/9412#discussioncomment-7245799
 */
  .format	= FI_CQ_FORMAT_DATA
};
static struct fid_av *av;
static struct fid_cq *cqr, *cqt; /* Receive and transmit queues */

/* Global variables for program logic */

/* fid_mr pointers must be stored for later cleanup */
static struct fid_mr **mregs = NULL;

/* Acknowledgements of received RMAs that aren't the one that's currently
 * being awaited. */
struct acks {
  uint64_t *keys;
  int size;
  int full;
};
static struct acks *acks;

/* Was related to address exchange */
#if 0
/* Addresses of the receive buffers of other nodes in each action sequence.
 * The sender needs this for sending the correct message data (an uint64_t),
 * which the receiver will use to check whether this is the message it is
 * expecting or not. */
struct bufAddr {
  uint64_t *addrlist; /* pointer that will get free()-d */
  uint64_t **addrs;   /* pointers to beginning of addrlist for each node ID */
};
static struct bufAddr *bufAddrs = NULL;
static size_t bufAddrsLen = 0; /* will be allocated during first prepare() */
#endif

static int isAsync = 1;
int ret;

/* Note: The order of invocation is not always prepare -> exec -> cleanup.
 *       It's also possible to prepare multiple aseqs and later execute them
 *       and clean them up.
 *       mnum must be static so the memory registrations of one prepare
 *       do not overwrite those of the previous prepare if there was no
 *       cleanup in between.
 *       It must be visible outside of RegisterMemory so that cleanup
 *       can reset it when it is invoked.
 * TODO: Making this global is not a good style... Any better way?
 */
static int mnum = 0;

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
  str = getenv("LAIK_FABRIC_PROV");
  if (str) {
    free(hints->fabric_attr->prov_name);
    hints->fabric_attr->prov_name = strdup(str);
  }

  /* Run-time behaviour depending on environment variables */
  str = getenv("LAIK_FABRIC_SYNC");
  if (str) isAsync = !atoi(str);
  laik_log(ll, "RMA mode: %csync", isAsync ? 'a' : ' ');

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
  ret = fi_getinfo(FI_VERSION(1,21), NULL, NULL, 0,
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
  PANIC_NZ(fi_cq_open(domain, &cq_attr, &cqr, NULL));
  PANIC_NZ(fi_cq_open(domain, &cq_attr, &cqt, NULL));
  PANIC_NZ(fi_ep_bind(ep, &av->fid, 0));
  PANIC_NZ(fi_ep_bind(ep, &cqr->fid, FI_RECV));
  PANIC_NZ(fi_ep_bind(ep, &cqt->fid, FI_TRANSMIT));
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
  if (!peers) laik_panic(allocfail);

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
    if (listen(sockfd, world_size))
      laik_log(LAIK_LL_Panic, "Failed to listen on socket: %s",
               strerror(errno));
    /* Insert our own address into address list */
    memcpy(peers, fi_addr, fi_addrlen);
    /* Get addresses of all nodes */
    int fds[world_size-1];
    for (int i = 0; i < world_size - 1; i++) {
      laik_log(ll, "%d out of %d connected...", i, world_size - 1);
      fds[i] = accept(sockfd, NULL, NULL);
      if (fds[i] < 0)
        laik_log(LAIK_LL_Panic, "Failed to accept connection: %s",
                 strerror(errno));
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

  acks = calloc(d.world_size, sizeof(struct acks));
  if (!acks) laik_panic(allocfail);

  return inst;
}

void add_fabRecvWait(Laik_Action **next, unsigned round, unsigned count) {
  Laik_A_FabRecvWait wait = {
    .h = {
      .type  = LAIK_AT_FabRecvWait,
      .len   = sizeof(Laik_A_FabRecvWait),
      .round = round,
      .tid   = 0,
      .mark  = 0
    },
    .count = count
  };
  memcpy(*next, &wait, sizeof(Laik_A_FabRecvWait));
  *next = nextAction((*next));
}

void add_fabSendWait(Laik_Action **next, unsigned round, unsigned count) {
  Laik_A_FabSendWait wait = {
    .h = {
      .type  = LAIK_AT_FabSendWait,
      .len   = sizeof(Laik_A_FabSendWait),
      .round = round,
      .tid   = 0,
      .mark  = 0
    },
    .count = count
  };
  memcpy(*next, &wait, sizeof(Laik_A_FabSendWait));
  *next = nextAction((*next));
}

void print_mregs(char *str) {
  /* If printf debugging isn't enabled, do nothing */
#ifdef PFDBG
  printf("%d: %s:\n", d.mylid, str);
  for (struct fid_mr **m = mregs; *m; m++)
    printf("%d: %p (%lx)\n", d.mylid, *m, fi_mr_key(*m));
#endif
}

void handle_cq_error(char *op, struct fid_cq *cq) {
  struct fi_cq_err_entry err;
  if (ret != -FI_EAVAIL)
    laik_log(LAIK_LL_Panic, "%s failed: %s", op, fi_strerror(ret));
  if (fi_cq_readerr(cq, &err, 0) != 1)
    laik_log(LAIK_LL_Panic,
             "%s failed:\nCQ error, but failed to retrieve error information",
             op);
  laik_log(LAIK_LL_Panic, "%s failed: CQ reported error: %s",
           op, fi_cq_strerror(cq, err.prov_errno, err.err_data, NULL, 0));
}

/* Registers memory buffers to libfabric, so that they can be accessed by RMA */
/* TODO: consider asynchronous instead of the default synchronous completion
 *       of memory registerations */
void fabric_aseq_RegisterMemory(Laik_ActionSeq *as) {
  /* TODO: Any more efficient way of storing memory registrations?
   *       How much memory does this really waste? Is it better or worse than
   *       introducing a backend-specific "register memory binding" action? */
  uint8_t regcount[d.world_size];
  memset(regcount, 0, d.world_size);
  mregs = realloc(mregs,
      (mnum + as->actionCount + 1) * sizeof(struct fid_mr*));
  if (!mregs) laik_panic(allocfail);

  Laik_TransitionContext *tc = as->context[0];

  int elemsize = tc->data->elemsize;
  Laik_Action *a = as->action;
  for (unsigned i = 0; i < as->actionCount; i++, a = nextAction(a)) {
    if (a->type == LAIK_AT_BufRecv || a->type == LAIK_AT_FabAsyncRecv) {
      Laik_A_BufRecv *aa = (Laik_A_BufRecv*) a;
      int reserve = aa->count * elemsize;
      uint64_t key = make_key(as->id, aa->from_rank,
                              regcount[aa->from_rank]++);
      D(printf("%d: REG  %p <== %lx\n", d.mylid, aa->buf, key));
      laik_log(ll, "Reserving %d * %d = %d bytes\n",
          aa->count, elemsize, reserve);
      PANIC_NZ(fi_mr_reg(
          domain, aa->buf, reserve*aa->count, FI_REMOTE_WRITE,
          0, key, 0, &mregs[mnum++], NULL));
    }
  }
  mregs[mnum] = NULL;

  print_mregs("MREGS IS NOW");
}

/* Currently unused and doesn't work */
#if 0
void fabric_aseq_ExchgMemoryRegs(Laik_ActionSeq *as) {
  /* TODO (measure): Does this use too much memory?
   *                 These are temporary arrays, but can become very big. */
  const size_t lMax = as->actionCount;
  uint64_t *otherRegs, *myRegs;
  size_t counts[d.world_size];
  myRegs = calloc(d.world_size * lMax, sizeof(uint64_t));
  otherRegs = calloc(d.world_size * lMax, sizeof(uint64_t));
  memset(counts, 0, d.world_size * sizeof(size_t));
  if (!myRegs || !otherRegs) laik_panic(allocfail);

  /* Gather arrays of this node's own memory regs */
  Laik_Action *a = as->action;
  for (unsigned i = 0; i < as->actionCount; i++, a = nextAction(a)) {
    if (a->type == LAIK_AT_BufRecv || a->type == LAIK_AT_FabAsyncRecv) {
      Laik_A_BufRecv *aa = (Laik_A_BufRecv*) a;
      int rank = aa->from_rank;
      myRegs[rank * lMax + counts[rank]++] = (uint64_t) aa->buf;
//      myRegs[rank][counts[rank]++] = (uint64_t) aa->buf;
      printf("%d: rank %d cnt %d\n", d.mylid, rank, counts[rank]);
    }
  }

  /* Post buffers for receives */
  for (int i = 0; i < d.world_size; i++) {
    if (i == d.mylid) continue;
    printf("%d: Recv %d bytes\n", d.mylid, lMax);
    RETRY(fi_recv(ep, &otherRegs[i * lMax], lMax * sizeof(uint64_t),
                  NULL, 0, NULL));
  }

  /* Send the array for each sender node to that node */
  /* Note: Using RMA here would require to first exchange the addresses of the
   *       otherRegs buffers on each node. That would be as many exchanges as
   *       just sending the registrations directly, so probably not worth it.
   */
  for (int i = 0; i < d.world_size; i++) {
    if (i == d.mylid) continue;
    printf("%d: Send %d bytes\n", d.mylid, counts[i] * sizeof(uint64_t));
    /*
    RETRY(fi_senddata(ep, myRegs[i], counts[i] * sizeof(uint64_t), NULL,
          i, i, NULL));
    */
    RETRY(fi_injectdata(ep, &myRegs[i * lMax], counts[i] * sizeof(uint64_t),
                        d.mylid, i));
    printf("%d: sent data:\n", d.mylid);
    for (int j = 0; j < counts[i]; j++)
      printf("%d: myRegs[%d][%d]: %p\n", d.mylid, i, j, myRegs[i * lMax + j]);
  }

  /* This structure is less wasteful of memory than myRegs, because
   * all recv addresses (there are at most as->actionCount of them, since
   * each BufRecv by another node from this node requires a matching
   * BufSend by this node) are stored in one list, rather than a 2D array
   * that allocates space for a lot of unused addresses.
   */
  uint64_t *addrlist = malloc(as->actionCount * sizeof(uint64_t));
  printf("%d: allocated addrlist %zu bytes\n",
      d.mylid, as->actionCount * sizeof(uint64_t));
  if (bufAddrsLen <= (unsigned) as->id) {
    bufAddrs = realloc(bufAddrs, (bufAddrsLen+1000) * sizeof(struct bufAddr));
    if (!bufAddrs) laik_panic("Malloc failed");
    bufAddrsLen += 1000;
  }
  bufAddrs[as->id].addrlist = addrlist;
  bufAddrs[as->id].addrs = malloc(d.world_size * sizeof(uint64_t*));
  if (!addrlist || !bufAddrs[as->id].addrs) laik_panic(allocfail);

  struct fi_cq_data_entry cq_buf;
  uint64_t *p = addrlist;
  for (int i = 0; i < d.world_size; i++) {
    int rank;
    if (i == d.mylid) continue;
    RETRYCQ(fi_cq_sread(cqr, (char*) &cq_buf, 1, NULL, -1), cqr);
    if (cq_buf.len == 0) continue;
    rank = cq_buf.data;
    printf("%d: received data:\n", d.mylid);
    for (int j = 0; j < cq_buf.len / sizeof(uint64_t); j++) {
      printf("%d: otherRegs[%d][%d]: %p\n", d.mylid, rank, j,
             otherRegs[rank * lMax + j]);
    }
    memcpy(p, &otherRegs[rank * lMax], cq_buf.len);
    printf("%d: copied %d bytes from %d to %p\n", d.mylid,
        cq_buf.len, rank, p);
    bufAddrs[as->id].addrs[rank] = p;
    if (cq_buf.len % sizeof(uint64_t) != 0) {
      printf("%d: MISALIGNED %d\n", d.mylid, cq_buf.len);
      assert(0);
    } /* alignment */
    p = (uint64_t*) (((char*) p) + cq_buf.len);
  }

  free(myRegs);
  free(otherRegs);

#if 0
  struct fi_cq_data_entry cq_buf;
  uint64_t *p = addrlist;
  for (int i = 0; i < d.world_size; i++) {
    if (i == d.mylid) continue;
    printf("%d: offset %zu\n", d.mylid, p - addrlist);
    RETRY(fi_recv(ep, p, lMax, NULL, 0, NULL));
    RETRYCQ(fi_cq_sread(cqr, (char*) &cq_buf, 1, NULL, -1), cqr);
    bufAddrs[as->id].addrs[cq_buf.data] = p;
    p += cq_buf.len;
  }
#endif

#ifdef PFDBG
  int cnt = p - addrlist;
  printf("%d: Got %d exchanged addresses:\n", d.mylid, cnt);
  for (int i = 0; i < cnt; i++) {
    printf("%d: (reading from %p)\n", d.mylid, bufAddrs[as->id].addrlist + i);
    printf("%d: %p\n", d.mylid, bufAddrs[as->id].addrlist[i]);
  }
#endif
  printf("------- AS %d\n", as->id);
}
#endif

/* Creates a new sequence that replaces BufSend and BufRecv with FabAsyncSend
 * and FabAsyncRecv, and adds a FabRmaWait at the end of any round that performs
 * at least one RMA */
void fabric_aseq_SplitAsyncActions(Laik_ActionSeq *as) {
  Laik_Action *newAction = malloc(as->bytesUsed
                                + as->roundCount * sizeof(Laik_A_FabRecvWait)
                                + sizeof(Laik_A_FabSendWait));
  if (!newAction) laik_panic(allocfail);
  Laik_Action *nextNewA = newAction;

  int sends = 0, recvs = 0;
  int lastRound = 1;
  unsigned waitCnt = 0;

  Laik_TransitionContext *tc = as->context[0];
  Laik_Action *a = as->action;
  for (unsigned i = 0; i < as->actionCount; i++, a = nextAction(a)) {
    /* Add FabRmaWait that waits for all RMAs of the last round to complete */
    if (a->round != lastRound) {
      if (recvs > 0) {
        add_fabRecvWait(&nextNewA, lastRound, recvs);
        waitCnt++;
      }
      recvs = 0;
      lastRound = a->round;
    }

    /* Count RMAs and copy actions into newAction */
    switch (a->type) {
      case LAIK_AT_BufSend:
        a->type = LAIK_AT_FabAsyncSend;
        sends++;
        break;
      case LAIK_AT_BufRecv:
        a->type = LAIK_AT_FabAsyncRecv;
        recvs++;
        break;
    }
    memcpy(nextNewA, a, a->len);
    nextNewA = nextAction(nextNewA);
  }
  /* Add a FabRmaWait after the final round, too */
  if (recvs > 0) {
    add_fabRecvWait(&nextNewA, lastRound, recvs);
    waitCnt++;
  }

  /* At the end of the action sequence, ensure that all the sends completed */
  add_fabSendWait(&nextNewA, lastRound, sends);

  /* Replace old action sequence with new and update AS information */
  free(as->action);
  as->action       = newAction;
  as->actionCount += waitCnt + 1;
  as->bytesUsed   += waitCnt * sizeof(Laik_A_FabRecvWait)
                  + sizeof(Laik_A_FabSendWait);
}

/* TODO: specify FI_ORDER_WAW in the TX context to guarantee correct ordering
 *       between two messages to the same address between the same nodes */
void ack_rma(uint64_t key) {
  int from = get_sender(key);
  D(printf("%d: ACK %lx from %d\n", d.mylid, key, from));
  /* We hope that this won't happen too often, since there is currently
   * no program logic that shrinks this array back, so there's a permanent
   * impact on performance because get_ack() has to search a larger array */
  if (acks[from].full == acks[from].size) {
    D(printf("%d: Resizing acks from %d\n", d.mylid, from));
    acks[from].keys = realloc(acks[from].keys,
                              (10 + acks[from].size) * sizeof(uint64_t));
    if (!acks[from].keys) laik_panic(allocfail);
    memset(acks[from].keys + acks[from].size, ack_empty, 10 * sizeof(uint64_t));
    acks[from].size += 10;
  }
  for (int i = 0; i < acks[from].size; i++) {
    if (acks[from].keys[i] == ack_empty) {
      acks[from].keys[i] = key;
      acks[from].full++;
      return;
    }
  }
  /* This should not be reached */
  assert(0);
}

/* Returns whether an ack for that message was found.
 * If it was found, this function also clears it. */
int pop_ack(int from, uint64_t key) {
  if (acks[from].full == 0) return 0;
  for (int i = 0; i < acks[from].size; i++) {
    if (acks[from].keys[i] == key) {
      acks[from].keys[i] = ack_empty;
      acks[from].full--;
      return 1;
    }
  }
  return 0;
}

void await_completions(struct fid_cq *cq, int num) {
  struct fi_cq_data_entry cq_buf;
//  printf("%ld: Awaiting %d completions on %p\n", d.mylid, num, cq);
  while (num > 0) {
    while ((ret = fi_cq_sread(cq, (char*) &cq_buf, 1, NULL, -1))
           == -FI_EAGAIN);
    assert(ret == 1);
    if (cq_buf.flags & FI_REMOTE_CQ_DATA) {
      ack_rma(cq_buf.data);
      continue;
    }
    num--;
  }
//  printf("Awaited %d completions on %p\n", num, cq);
}

uint64_t make_key(int id, int send_node, uint8_t seq) {
  return (((uint64_t) id) << 40) + (((uint64_t) send_node) << 8) + seq;
}

int get_sender(uint64_t key) {
  return (key & 0xFFFF0) >> 8;
}

unsigned get_aseq(struct fid_mr *mr) {
  return (fi_mr_key(mr) >> 40);
}

void barrier(void) {
  uint8_t tmp;
  if (d.mylid == 0) {
    for (int i = 1; i < d.world_size; i++) {
      while ((ret = fi_recv(ep, &tmp, 1, NULL, i, NULL)) == -FI_EAGAIN);
      assert(ret >= 0);
    }
    await_completions(cqr, d.world_size - 1);
    for (int i = 1; i < d.world_size; i++) {
      while ((ret = fi_send(ep, &tmp, 1, NULL, i, NULL)) == -FI_EAGAIN);
      assert(ret >= 0);
    }
    await_completions(cqt, d.world_size - 1);
  } else {
    while ((ret = fi_send(ep, &tmp, 1, NULL, 0, NULL)) == -FI_EAGAIN);
    assert(ret >= 0);
    await_completions(cqt, 1);
    while ((ret = fi_recv(ep, &tmp, 1, NULL, 0, NULL)) == -FI_EAGAIN);
    assert(ret >= 0);
    await_completions(cqr, 1);
  }
}

void fabric_prepare(Laik_ActionSeq *as) {
  uint64_t tmp = 0;
  if (as->actionCount == 0) {
    laik_aseq_calc_stats(as);
    goto join;
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

  if (isAsync) {
    fabric_aseq_SplitAsyncActions(as);
    laik_log_ActionSeqIfChanged(true, as, "After splitting async actions");
  }

  laik_aseq_calc_stats(as);

join:
  /* Join-point */
  /* TODO: exchange new and removed nodes */
  /* TODO: proper error handling */
  D(printf("%d: START JOIN %d\n", d.mylid, as->id));
  if (d.mylid == 0) {
    for (int i = 1; i < d.world_size; i++) {
      while ((ret = fi_recv(ep, &tmp, 4, NULL, i, NULL)) == -FI_EAGAIN);
      assert(ret >= 0);
    }
    await_completions(cqr, d.world_size - 1);
    for (int i = 1; i < d.world_size; i++) {
      while ((ret = fi_send(ep, &tmp, 4, NULL, i, NULL)) == -FI_EAGAIN);
      assert(ret >= 0);
    }
    await_completions(cqt, d.world_size - 1);
  } else {
    while ((ret = fi_send(ep, &tmp, 4, NULL, 0, NULL)) == -FI_EAGAIN);
    assert(ret >= 0);
    await_completions(cqt, 1);
    while ((ret = fi_recv(ep, &tmp, 4, NULL, 0, NULL)) == -FI_EAGAIN);
    assert(ret >= 0);
    await_completions(cqr, 1);
  }

  /* TODO: update world size and AV after getting new node list */
  fabric_aseq_RegisterMemory(as);

  /* Ensures that all memory bindings are registered before any data gets
   * exchanged. If this is not ensured, apparently messages sent before
   * the memory is registered simply do not arrive (or at least do not generate
   * a completion queue entry).
   *
   * TODO: find whether libfabric documentation has anything to say about this
   */
  barrier();

  D(printf("%d: END   JOIN %d\n", d.mylid, as->id));

}

void fabric_exec(Laik_ActionSeq *as) {
  D(printf("%d: EXEC %d\n", d.mylid, as->id));
  Laik_Action *a = as->action;
  Laik_TransitionContext *tc = as->context[0];
  Laik_MappingList* fromList = tc->fromList;
  Laik_MappingList* toList = tc->toList;

  int elemsize = tc->data->elemsize;
  struct fi_cq_data_entry cq_buf;
  uint8_t sndcount[d.world_size], rcvcount[d.world_size];
  memset(sndcount, 0, d.world_size);
  memset(rcvcount, 0, d.world_size);
  for (unsigned i = 0; i < as->actionCount; i++, a = nextAction(a)) {
    Laik_BackendAction *ba = (Laik_BackendAction*) a;
    switch (a->type) {
      case LAIK_AT_Nop: break;
      case LAIK_AT_FabAsyncRecv: {
        Laik_A_FabAsyncRecv *aa = (Laik_A_FabAsyncRecv*) a;
        uint64_t key = make_key(as->id, aa->from_rank, rcvcount[aa->from_rank]);
        D(printf("%d: Waiting for recv from %d (key %lx)\n",
              d.mylid, aa->from_rank, key));
        if (pop_ack(aa->from_rank, key)) {
          /* Data that we're waiting for has already been received */
          D(printf("%d: %lx already ack'd!\n", d.mylid, key));
          goto recv_done;
        }
        while (1) { /* Data not received yet, await new completion reports */
          RETRYCQ(fi_cq_sread(cqr, (char*) &cq_buf, 1, NULL, -1), cqr);
          D(printf("%d: Waiting for %lx from %d, got %lx from %d\n", d.mylid,
                key, aa->from_rank, cq_buf.data, get_sender(cq_buf.data)));
          if (cq_buf.data == key) goto recv_done;
          else ack_rma(cq_buf.data);
        }
recv_done:
        rcvcount[aa->from_rank]++;
        break;
      }
      case LAIK_AT_FabAsyncSend: {
        Laik_A_BufSend *aa = (Laik_A_BufSend*) a;
        uint64_t key = make_key(as->id, d.mylid, sndcount[aa->to_rank]++);
        D(printf("%d: SEND ==> %d (%lx)\n", d.mylid, aa->to_rank, key));
        while ((ret = fi_writedata(ep, aa->buf, elemsize * aa->count, NULL,
                key, aa->to_rank, 0, key, NULL))
               == -FI_EAGAIN);
        if (ret)
          laik_log(LAIK_LL_Panic,
              "fi_writedata() failed: %s", fi_strerror(ret));
        break;
      }
      case LAIK_AT_FabRecvWait: {
        /* TODO: Is there any kind of ordering where this makes sense? */
        break;
      }
      case LAIK_AT_FabSendWait: {
        Laik_A_FabSendWait *aa = (Laik_A_FabSendWait*) a;
        D(printf("%d: Waiting for %d send completions\n", d.mylid, aa->count));
        unsigned completions = 0;
        while (completions < aa->count) {
          ret = fi_cq_sread(cqt, (char*) &cq_buf, 1, NULL, -1);
          if (ret == -FI_EAGAIN) continue;
          assert(ret > 0);
          /* TODO: either do something with the retrieved information
           *       or replace the CQ with a counter */
          completions++;
        }
        D(printf("%d: Sending done\n", d.mylid));
        break;
      }
/* TODO, possibly?
 * Reduce,
 * GroupReduce,
 * RecvAndUnpack,
 * MapRecvAndUnpack,
 * PackAndSend,
 * MapPackAndSend
 */
      case LAIK_AT_CopyFromBuf:
        for (unsigned int i = 0; i < ba->count; i++)
          memcpy(ba->ce[i].ptr,
                 ba->fromBuf + ba->ce[i].offset,
                 ba->ce[i].bytes);
        break;
      case LAIK_AT_CopyToBuf:
        for (unsigned int i = 0; i < ba->count; i++)
          memcpy(ba->toBuf + ba->ce[i].offset,
                 ba->ce[i].ptr,
                 ba->ce[i].bytes);
        break;
      case LAIK_AT_PackToBuf:
        laik_exec_pack(ba, ba->map);
        break;
      case LAIK_AT_MapPackToBuf: {
        assert(ba->fromMapNo < fromList->count);
        Laik_Mapping* fromMap = &(fromList->map[ba->fromMapNo]);
        assert(fromMap->base != 0);
        laik_exec_pack(ba, fromMap);
        break;
      }
      case LAIK_AT_UnpackFromBuf:
        laik_exec_unpack(ba, ba->map);
        break;
      case LAIK_AT_MapUnpackFromBuf: {
        assert(ba->toMapNo < toList->count);
        Laik_Mapping* toMap = &(toList->map[ba->toMapNo]);
        assert(toMap->base);
        laik_exec_unpack(ba, toMap);
        break;
      }
      case LAIK_AT_RBufLocalReduce:
        assert(ba->bufID < ASEQ_BUFFER_MAX);
        assert(ba->dtype->reduce != 0);
        (ba->dtype->reduce)(ba->toBuf, ba->toBuf,
            as->buf[ba->bufID] + ba->offset, ba->count, ba->redOp);
        break;
      case LAIK_AT_RBufCopy:
        assert(ba->bufID < ASEQ_BUFFER_MAX);
        memcpy(ba->toBuf, as->buf[ba->bufID] + ba->offset, ba->count * elemsize);
        break;
      case LAIK_AT_BufCopy:
        memcpy(ba->toBuf, ba->fromBuf, ba->count * elemsize);
        break;
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
  int nclosed;

  /* Make sure that no node enters cleanup while the others are still executing
   * the action sequence and might need to use its registered memory */
  barrier();

  D(printf("%d: CLEANUP %d\n", d.mylid, as->id));

  print_mregs("MREGS BEFORE CLEANUP");

  /* Clean up memory registrations for current aseq */
  /* Memory registrations are sorted by aseq ID in ascending order */
  struct fid_mr **mr_first = mregs;
  struct fid_mr **mr_last;
  for (; *mr_first && get_aseq(*mr_first) < (unsigned) as->id; mr_first++);
  for (mr_last = mr_first;
       *mr_last && get_aseq(*mr_last) == (unsigned) as->id;
       mr_last++) {
    PANIC_NZ(fi_close((struct fid *) *mr_last));
  }
  nclosed = mr_last - mr_first;
  mnum -= nclosed;
  if (nclosed > 0) {
    /* Move all later memory registrations into the empty space left by the
     * closed memory registrations */
    do {
      *(mr_first++) = *mr_last;
    } while (*(mr_last++));
  }

  print_mregs("MREGS AFTER CLEANUP");
}

void fabric_finalize(Laik_Instance *inst) {
  /* TODO: Final cleanup */
  (void) inst;

  free(acks);
  free(mregs);
  fi_close((struct fid *) ep);
  fi_close((struct fid *) av);
  fi_close((struct fid *) cqr);
  fi_close((struct fid *) cqt);
  fi_close((struct fid *) domain);
  fi_close((struct fid *) fabric);
  fi_freeinfo(info);
}

#endif /* USE_FABRIC */

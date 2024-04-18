#ifdef USE_FABRIC
#include "laik-internal.h"
#include "laik-backend-fabric.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>

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

/* Declare the check_local() function from src/backend_tcp2.c, which we are
 * re-using because I couldn't find a better way to do it with libfabric
 */
bool check_local(char *host);

static struct fi_info *info;
static struct fid_fabric *fabric;
static struct fid_domain *domain;
static struct fid_ep *ep;
/* TODO: Can we use FI_SYMMETRIC? */
static struct fi_av_attr av_attr = { .type = FI_AV_TABLE };
static struct fi_cq_attr cq_attr = { 0 };
static struct fi_eq_attr eq_attr = { 0 };
static struct fid_av *av;
static struct fid_cq *cq;
static struct fid_eq *eq;

static Laik_Backend laik_backend = {
  .name = "Libfabric Backend",
  .prepare = fabric_prepare,
  .exec = fabric_exec,
  .cleanup = fabric_cleanup,
  .finalize = fabric_finalize
};

Laik_Instance *laik_init_fabric(int *argc, char ***argv) {
  char *str;
  int ret;

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
  ret = fi_getinfo(FI_VERSION(1,21), home_host, home_port_str, 0,
      hints, &info);
  if (ret || !info) laik_panic("No suitable fabric provider found!");
  laik_log(ll, "Selected fabric \"%s\", domain \"%s\"",
      info->fabric_attr->name, info->domain_attr->name);
  fi_freeinfo(hints);

  /* Set up address vector */
  PANIC_NZ(fi_fabric(info->fabric_attr, &fabric, NULL));
  PANIC_NZ(fi_domain(fabric, info, &domain, NULL));
  PANIC_NZ(fi_av_open(domain, &av_attr, &av, NULL));

  /* Sync node addresses over regular sockets, using a similar process as the
   * tcp2 backend, since libfabric features aren't needed here.
   *
   * Also, I couldn't figure out how to get the address that an endpoint in
   * libfabric is bound to; fi_getname didn't work the way I expected, it
   * returned 24 bytes of address, which corresponds neither to IPv4 nor IPv6...
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
    /* Try binding a socket to the home address */
    laik_log(ll, "Trying to become master");
    is_master = (bind(sockfd, res->ai_addr, res->ai_addrlen) == 0);
  }

  if (is_master) {
    laik_log(ll, "Became master!");
    /* If we are master, create listening socket on home node */
    if (listen(sockfd, world_size)) laik_panic("Failed to listen on socket");
    for (int i = 0; i < world_size - 1; i++) {
      struct sockaddr addr;
      socklen_t addrlen = sizeof(addr);
      laik_log(ll, "%d out of %d connected...", i, world_size - 1);
      int newfd = accept(sockfd, &addr, &addrlen);
      if (newfd < 0) laik_panic("Failed to accept connection");

      laik_log(ll, "Got addr of length %d:", addrlen);
      laik_log_hexdump(ll, addrlen, &addr);
      /* TODO: Add the address to the address vector */

      close(newfd);
    }
    close(sockfd);
  } else {
    laik_log(ll, "Didn't become master!");
    laik_log(ll, "Connecting to:");
    laik_log_hexdump(ll, res->ai_addrlen, res->ai_addr);
    if (connect(sockfd, res->ai_addr, res->ai_addrlen)) {
      laik_log(LAIK_LL_Error, "Failed to connect: %s", strerror(errno));
      exit(1);
    }
  }

  /* TODO: Send address vector to every non-master node
   *       (If that's necessary?)
   *
   *       Also send the size of the address vector?
   *       (Needed for LAIK initialization)
   */
  
  /* Set up endpoint for RMA */

  /* Open the endpoint */
  PANIC_NZ(fi_endpoint(domain, info, &ep, NULL));

  /* Bind it to an event queue and completion queue */
  PANIC_NZ(fi_cq_open(domain, &cq_attr, &cq, NULL));
  PANIC_NZ(fi_eq_open(fabric, &eq_attr, &eq, NULL));
  PANIC_NZ(fi_ep_bind(ep, &cq->fid, FI_TRANSMIT|FI_RECV));
  PANIC_NZ(fi_ep_bind(ep, &eq->fid, 0)); /* TODO: is 0 OK here */
  PANIC_NZ(fi_enable(ep));

  char addr[160];
  size_t addrlen = 160;
  PANIC_NZ(fi_getname(&ep->fid, addr, &addrlen));
  laik_log(1, "Got addr of length %zu: %02hhx %02hhx %02hhx", addrlen, addr[0],
      addr[1], addr[2]);

  /* TODO: initialize LAIK */
  return NULL;
}


/* TODO: Do any backend-specific actions make sense? */

void fabric_prepare(Laik_ActionSeq *as) {
  (void) as;
  /* TODO:
   *   - laik_aseq_whatever() to transform action sequence
   *   - Set up RDMAs
   */
}

void fabric_exec(Laik_ActionSeq *as) {
  (void) as;
  /* TODO: switch statement with the actions */
}

void fabric_cleanup(Laik_ActionSeq *as) {
  (void) as;
  /* TODO: Clean up RDMAs */
}

void fabric_finalize(Laik_Instance *inst) {
  (void) inst;
  /* TODO: Final cleanup */
}

#endif /* USE_FABRIC */

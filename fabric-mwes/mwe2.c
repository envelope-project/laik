#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>

#define PANIC_NZ(a) if ((ret = a)) panic("" #a "", fi_strerror(ret));

static struct fi_info *info;
static struct fid_fabric *fabric;
static struct fid_domain *domain;
static struct fid_ep *ep;
static struct fi_av_attr av_attr = { 0 };
static struct fi_cq_attr cq_attr = { 0 };
static struct fi_eq_attr eq_attr = { 0 };
static struct fid_av *av;
static struct fid_cq *cq;
static struct fid_eq *eq;
int ret;

/* Collective operations stuff */
static struct fid_av_set *av_set;
static struct fi_addr_t	coll;
static struct fid_mc *mc;

void panic(char *f, const char *msg) {
  fprintf(stderr, "%s failed: %s\n", f, msg);
  exit(1);
}

void hexdump(int len, void *buf) {
  for (int i = 0; i < len; i++) printf("%02hhx ", ((char*)buf)[i]);
  printf("\n");
}

/*
 * is_server => A
 *
 * A                             B
 *            recv()
 * bufA ----------------------> bufB
 * zero()
 *            recv()
 * bufA <---------------------- bufB
 *
 * How to synchronize?
 *
 * If I receive:
 * Before I start receive, I must know that data is ready
 * Before I overwrite data, I must know that everyone has finished receiving it
 *
 * If I send:
 * I know that my data is ready, unless someone does an RMA to it
 * I need a way to get notified when someone starts an RMA on my data,
 * so that I can wait until it finishes before proceeding
 */

#if 0
/*
msg_exchange_a() {
  char bufA[16];
  barrier(1);
  recvFrom(bufB);
  memset(bufA, 0, 16);
  barrier(2);
}
*/

msg_exchange_a() {
  char bufA[16];
  fi_barrier(ep, coll, NULL);
  fi_read(ep, bufA, 16, 0, 1, 0, 0, NULL);

}

/*
msg_exchange_b() {
  char bufB[16] = "Hello world!   ";
  barrier(1);
  barrier(2);
  recvFrom(bufA);
}
*/
#endif

void barrier() {
  char buf[32];
  PANIC_NZ(fi_barrier(ep, coll, NULL));
  PANIC_NZ(fi_cq_sread(cq, cq_buf, 1, NULL, -1));
}

int main(int argc, char **argv) { 
  char *host = "localhost";
  int is_server = argc <= 1;
  char *port = is_server ? "1234" : "4321" ;

  /* Select fabric */
  struct fi_info *hints = fi_allocinfo();
  hints->ep_attr->type = FI_EP_RDM;
  hints->caps = FI_MSG | FI_RMA;
  PANIC_NZ(fi_getinfo(FI_VERSION(1,21), host, port, FI_SOURCE, hints, &info));
  printf("Selected fabric \"%s\", domain \"%s\"\n",
      info->fabric_attr->name, info->domain_attr->name);
  fi_freeinfo(hints);

  /* Set up address vector */
  PANIC_NZ(fi_fabric(info->fabric_attr, &fabric, NULL));
  PANIC_NZ(fi_domain(fabric, info, &domain, NULL));
  av_attr.type = FI_AV_TABLE;
  av_attr.count = 2;
  PANIC_NZ(fi_av_open(domain, &av_attr, &av, NULL));

  /* Open the endpoint, bind it to an EQ, CQ, and AV*/
  PANIC_NZ(fi_endpoint(domain, info, &ep, NULL));
  cq_attr.wait_obj = FI_WAIT_UNSPEC;
  PANIC_NZ(fi_cq_open(domain, &cq_attr, &cq, NULL));
  PANIC_NZ(fi_eq_open(fabric, &eq_attr, &eq, NULL));
  PANIC_NZ(fi_ep_bind(ep, &av->fid, 0));
  PANIC_NZ(fi_ep_bind(ep, &cq->fid, FI_TRANSMIT|FI_RECV));
  PANIC_NZ(fi_ep_bind(ep, &eq->fid, 0));
  PANIC_NZ(fi_enable(ep));

  /* Get the address of the endpoint */
  char fi_addr[160]; 
  size_t fi_addrlen = 160;
  PANIC_NZ(fi_getname(&ep->fid, fi_addr, &fi_addrlen));
  printf("Got libfabric EP addr of length %zu:\n", fi_addrlen);
  hexdump(fi_addrlen, fi_addr);

  /* Insert client and server address into AV */
  /* Obviously not the right way to do this, but the shortest way */
  char *server_port = "\x04\xd2";
  char *client_port = "\x10\xe1";
  memcpy(fi_addr + 2, server_port, 2);
  ret = fi_av_insert(av, fi_addr, 1, NULL, 0, NULL);
  assert(ret == 1);
  memcpy(fi_addr + 2, client_port, 2);
  ret = fi_av_insert(av, fi_addr, 1, NULL, 0, NULL);
  assert(ret == 1);

  /* Set up collective set */
  char evbuf[32];
  struct fi_av_set_attr av_set_attr = {
    .count	= 2,
    /*
    .start_addr	= 0,
    .end_addr	= 1,
    .stride	= 1,
    */
    .stride	= 0,
    .comm_key_size = 0,
    .comm_key	= NULL,
    .flags	= FI_UNIVERSE | FI_BARRIER_SET,
  };
  PANIC_NZ(fi_av_set(av, &av_set_attr, &av_set, NULL));
  PANIC_NZ(fi_av_set_addr(av_set, &coll));

  /* Join collective */
  uint32_t event = 0;
  PANIC_NZ(fi_join_collective(ep, coll, av_set, 0, &mc, NULL));
  while (event != FI_JOIN_COMPLETE) {
    fi_eq_sread(eq, &event, evbuf, 32, -1, 0);
  }
  printf("Joined the collective!\n");

  /* Try to exchange a message */
  if (is_server) {
    char buf[6] = { 0 };
    struct fid_mr *mr;

    /* Register memory */
    PANIC_NZ(fi_mr_reg(domain, buf, 6, FI_REMOTE_READ | FI_REMOTE_WRITE, 0,
        0, FI_RMA_EVENT, &mr, NULL));
    PANIC_NZ(fi_mr_bind(mr, (struct fid *) cntr, FI_REMOTE_WRITE));
    PANIC_NZ(fi_mr_enable(mr));

    /* Wait for RMA to complete */
    printf("Barrier call!\n");
    barrier();

    fi_close((struct fid *) mr);
    printf("Got message: %s\n", buf);
  } else {
    char cq_buf[160];
    char buf[6] = "Hello";
    while ((ret = fi_write(ep, buf, 6, NULL, 0, 0, 0, NULL)) == -FI_EAGAIN);
    if (ret) panic("fi_write()", fi_strerror(ret));
    printf("Waiting for fi_write() to complete...\n");
    PANIC_NZ(fi_cq_sread(cq, cq_buf, 1, NULL, -1));
    printf("Barrier call!\n");
    barrier();
  }

  fi_close((struct fid *) ep);
  fi_close((struct fid *) av);
  fi_close((struct fid *) eq);
  fi_close((struct fid *) cq);
  fi_close((struct fid *) domain);
  fi_close((struct fid *) fabric);
  fi_freeinfo(info);
  return 0;
}

/*
 * This file is part of the LAIK library.
 * Copyright (c) 2020 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 or later.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Planned design (to be implemented / updated)
 *
 * The protocol used at TCP level among processes should enable easy debugging
 * and playing with ideas (eg. via 'nc'/'telnet'), performance is low priority.
 * Thus, it is based on text and line separation.
 *
 * When messages end in <value> at end of line, it can be send as
 * - "+<payload length in bytes> R\n<raw bytes>\n"
 * - "+<payload lentgh in bytes> H\n" and multiple lines of
 *   " [(<offset>)] [<hex>*] [# <comment>]\n"
 * - for data of specific type, <hex> can be replaced by element value converted
 *   into ASCII representation, providing the element type instead of H, e.g.
 *   D/F/U32/U64/I32/I64 for double, float, (un)signed 32/64 bit, respectively
 *
 * Messages can be preceded by lines starting with "# ...\n" as comments, which
 * are ignored but may be written as log at receiver side for debugging
 *
 * Startup:
 * - home process (location ID 0) is the process started on LAIK_TCP2_HOST
 *   (default: localhost) which aquired LAIK_TCP2_PORT for listening
 * - other processes register with home process to join
 * - home process waits for LAIK_SIZE (default: 1) processes to join before
 *   finishing initialization and giving control to application
 *
 * Registration:
 * - open own listening port (usually randomly assigned by OS) at <myport>
 * - connect to home process; this may block until home can accept connections
 * - send "register <mylocation> <myhost> <myport>\n"
 *     - <mylocation> can be any string, but should be unique
 *     - if <myhost> is not specified, it is identified as connecting peer
 * - home sends an ID line for the new assigned id of registering process
 *     "id <id> <location> <host> <port>\n"
 * - afterwards, home sends the following message types in arbitrary order:
 *   - further ID lines, for each registered process
 *   - config lines "config <key> <value>\n"
 *   - serialized objects "object <type> <name> <version> <refcount> <value>\n"
 * - at end: home sends current compute phase "phase <phaseid> <iteration>\n"
 * - give back control to application, connection can stay open
 *
 * Elasticity:
 * - LAIK checks backend for processes wanting to join at compute phase change
 * - processes tell master about reached phase and ask for new IDs
 *     "resize <phaseid> <maxid>"
 * - master answers
 *   - new ID lines of processes joining
 *   - ids to be removed: "remove <id>"
 *   - finishes with "done"
 * - control given back to application, to process resize request
 * 
 * Data exchange:
 * - always done directly between 2 processes, using any existing connection
 * - if no connection exists yet
 *     - receiver always waits to be connected
 *     - sender connects to listening port of receiver, sends "id <id>\n"
 * - sender sends "data <container name> <start index> <element count> <value>"
 * - connections can be used bidirectionally
  * 
 * Sync:
 * - two phases:
 *   - send changed objects to home process
 *   - receiving changes from home process
 * - start with "sync <id>\n"
 * - multiple "object <type> <name> <version> <refcount>[ <value>]\n"
 *   sending <value> is optional if only <refcount> changes
 * - end with "done\n"
 * - objects may be released if all <refcounts> are 0
 *
 * Deregistration: todo
 *
 * External commands: todo
 */


#ifdef USE_TCP2

#include "laik-internal.h"
#include "laik-backend-tcp2.h"

#include <assert.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
// for VSC to see def of addrinfo
#ifndef __USE_XOPEN2K
#define __USE_XOPEN2K 1
#endif
#include <netdb.h>

// defaults
#define TCP2_PORT 7777

#define MAX_PEERS 256
#define MAX_LOOPFD 256
// receive buffer length
#define RBUF_LEN 256

// forward decl
void tcp2_exec(Laik_ActionSeq* as);
void tcp2_sync(Laik_KVStore* kvs);

typedef struct _InstData InstData;

// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend = {
    .name = "Dynamic TCP2 Backend",
    .exec = tcp2_exec,
    .sync = tcp2_sync
};

static Laik_Instance* instance = 0;

// structs for instance

typedef struct {
    int fd;         // -1 if not connected
    int port;       // port to connect to at host
    char* host;     // remote host, if 0 localhost
    char* location; // location string of peer
    int rbuf_left;  // valid bytes in receive buffer
    char rbuf[RBUF_LEN]; // receive buffer
} Peer;

// registrations for active fds in event loop
typedef void (*loop_cb_t)(InstData* d, int fd);
typedef struct {
    int id;          // peer
    loop_cb_t cb;
} LoopCB;

struct _InstData {
    int id;           // my id in world
    char* host;       // my hostname
    char* location;   // my location
    int listenfd;     // file descriptor for listening to connections
    int listenport;   // port we listen at (random unless master)
    int maxid;        // highest seen id
    int phase;        // current phase

    // event loop
    int maxfds;       // highest fd in rset
    fd_set rset;      // read set for select
    int exit;         // set to exit event loop
    LoopCB cb[MAX_LOOPFD];

    int peers;        // number of active peers, can be 0 only at master
    Peer peer[0];
};

typedef struct {
    int count;
    int* id;
} GroupData;


// event loop functions

void add_rfd(InstData* d, int fd, loop_cb_t cb)
{
    assert(fd < MAX_LOOPFD);
    assert(d->cb[fd].cb == 0);

    FD_SET(fd, &d->rset);
    if (fd > d->maxfds) d->maxfds = fd;
    d->cb[fd].cb = cb;
    d->cb[fd].id = -1;
}

void rm_rfd(InstData* d, int fd)
{
    assert(fd < MAX_LOOPFD);
    assert(d->cb[fd].cb != 0);

    FD_CLR(fd, &d->rset);
    if (fd == d->maxfds)
        while(!FD_ISSET(d->maxfds, &d->rset)) d->maxfds--;
    d->cb[fd].cb = 0;
}

void run_loop(InstData* d)
{
    d->exit = 0;
    while(d->exit == 0) {
        fd_set rset = d->rset;
        if (select(d->maxfds+1, &rset, 0, 0, 0) >= 0) {
            for(int i = 0; i <= d->maxfds; i++)
                if (FD_ISSET(i, &rset)) {
                    assert(d->cb[i].cb != 0);
                    (d->cb[i].cb)(d, i);
                }
        }
    }
}


// helper functions

// check if hostname maps to localhost by binding a socket at arbitrary port
int check_local(char* host)
{
    struct addrinfo hints, *info, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int ret;
    if ((ret = getaddrinfo(host, 0, &hints, &info)) != 0) {
        // host not found: not fatal here
        laik_log(1, "TCP2 check_local - host %s not found", host);
    }
    int fd = -1;
    for(p = info; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == -1) continue;
        if (p->ai_addr->sa_family == AF_INET)
            ((struct sockaddr_in*)(p->ai_addr))->sin_port = 0;
        else if (p->ai_addr->sa_family == AF_INET6)
            ((struct sockaddr_in6*)(p->ai_addr))->sin6_port = 0;
        else {
            close(fd);
            continue;
        }
        if (bind(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
    }
    if (p) close(fd);
    return (p != 0);
}

// forward decl
void got_data(InstData* d, int fd);

// make sure we have an open connection to peer <id>
void check_id(InstData* d, int id)
{
    assert(id < MAX_PEERS);
    if (d->peer[id].fd >= 0) return; // connected

    assert(d->peer[id].port >= 0);
    char port[20];
    sprintf(port, "%d", d->peer[id].port);

    struct addrinfo hints, *info, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int ret;
    if ((ret = getaddrinfo(d->peer[id].host, port, &hints, &info)) != 0) {
        laik_log(LAIK_LL_Panic, "TCP2 host %s not found - getaddrinfo %s",
                 d->peer[id].host, gai_strerror(ret));
        exit(1);
    }
    int fd = -1;
    for(p = info; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == -1) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
    }
    if (p == 0) {
        laik_log(LAIK_LL_Panic, "TCP2 cannot connect to ID %d (host %s, port %d)",
                 id, d->peer[id].host, d->peer[id].port);
        exit(1);
    }
    d->peer[id].fd = fd;
    d->peer[id].rbuf_left = 0;
    add_rfd(d, fd, got_data);
    d->cb[fd].id = id;
}


void send_msg(InstData* d, int id, char* msg)
{
    assert(id >= 0);
    check_id(d, id);
    int len = strlen(msg);
    write(d->peer[id].fd, msg, len);
    //laik_log(1, "TCP2 Sent msg '%s' (len %d) to ID %d (FD %d)\n",
    //        msg, len, id, d->peer[id].fd);
}


void got_cmd(InstData* d, int fd, char* msg, int len)
{
    int id = d->cb[fd].id;
    laik_log(1, "TCP2 Got cmd '%s' (len %d) from ID %d (FD %d)\n",
            msg, len, id, fd);

    if (msg[0] == 'r') {
        // register <location> <host> <port>

        // ignore if not master
        if (d->id != 0) return;

        char cmd[20], l[50], h[50];
        int p;
        if (sscanf(msg, "%20s %50s %50s %d", cmd, l, h, &p) < 4) {
            laik_log(LAIK_LL_Panic, "cannot parse register command '%s'", msg);
            return;
        }

        id = ++d->maxid;
        d->cb[fd].id = id;
        assert(id < MAX_PEERS);
        laik_log(1, "TCP2 registered new ID %d: location %s at host %s port %d",
                 id, l, h, p);

        assert(d->peer[id].port == -1);
        d->peer[id].fd = fd;
        d->peer[id].host = strdup(h);
        d->peer[id].location = strdup(l);
        d->peer[id].port = p;
        d->peer[id].rbuf_left = 0;

        // send ID info: "id <id> <location> <host> <port>"
        // new registered id to all already registered peers
        sprintf(msg, "id %d %s %s %d\n", id, l, h, p);
        for(int i = 1; i <= d->maxid; i++)
            send_msg(d, i, msg);
        for(int i = 0; i < d->maxid; i++) {
            sprintf(msg, "id %d %s %s %d\n", i,
                    d->peer[i].location, d->peer[i].host, d->peer[i].port);
            send_msg(d, id, msg);
        }

        d->peers++;
        d->exit = 1;
        return;
    }

    if (msg[0] == 'i') {
        // id <id> <location> <host> <port>

        // ignore if master
        if (d->id == 0) return;

        char cmd[20], l[50], h[50];
        int p;
        if (sscanf(msg, "%20s %d %50s %50s %d", cmd, &id, l, h, &p) < 5) {
            laik_log(LAIK_LL_Panic, "cannot parse id command '%s'", msg);
            return;
        }

        assert((id >= 0) && (id < MAX_PEERS));
        if (d->id < 0) {
            // is this my id?
            if (strcmp(d->location, l) == 0)
                d->id = id;
        }
        if (d->peer[id].location != 0) {
            assert(strcmp(d->peer[id].location, l) == 0);
            assert(strcmp(d->peer[id].host, h) == 0);
            assert(d->peer[id].port == p);
        }
        else {
            d->peer[id].host = strdup(h);
            d->peer[id].location = strdup(l);
            d->peer[id].port = p;
            if (id != d->id) d->peers++;
        }
        if (id > d->maxid) d->maxid = id;
        laik_log(1, "TCP2 seen ID %d (location %s%s), active peers %d",
                 id, l, (id == d->id) ? ", my ID":"", d->peers);
        return;
    }

    if (msg[0] == 'p') {
        // phase <phaseid>

        // ignore if master
        if (d->id == 0) return;

        char cmd[20];
        int phase;
        if (sscanf(msg, "%20s %d", cmd, &phase) < 2) {
            laik_log(LAIK_LL_Panic, "cannot parse phase command '%s'", msg);
            return;
        }
        laik_log(1, "TCP2 got phase %d", phase);
        d->phase = phase;
        d->exit = 1;
        return;
    }

    laik_log(LAIK_LL_Panic, "TCP2 from ID %d unknown msg '%s'", id, msg);
}


void got_data(InstData* d, int fd)
{
    int id = d->cb[fd].id;

    // use a per-id receive buffer to not mix partially sent commands
    char* rbuf = d->peer[id].rbuf;
    int left = d->peer[id].rbuf_left;

    int len = read(fd, rbuf + left, RBUF_LEN - left);
    if (len == -1) {
        laik_log(1, "TCP2 warning: read error on FD %d\n", fd);
        return;
    }
    if (len == 0) {
        // other side closed connection

        // still a command in the buffer?
        // can only be one, as left-over bytes in buffer is just one line
        if (left > 0) {
            got_cmd(d, fd, rbuf, left);
            d->peer[id].rbuf_left = 0;
        }

        close(fd);
        rm_rfd(d, fd);

        if (id >= 0)
            d->peer[id].fd = -1;

        laik_log(1, "TCP2 FD %d closed (peer ID %d, location %s)\n",
                 fd, id, (id >= 0) ? d->peer[id].location : "-");
        return;
    }

    laik_log(1, "TCP2 got_data(FD %d, peer ID %d): read %d bytes (left: %d)\n",
             fd, id, len, left);

    int pos1 = 0, pos2 = 0;
    while(pos2 < left + len) {
        if (rbuf[pos2] == '\n') {
            rbuf[pos2] = 0;
            got_cmd(d, fd, rbuf + pos1, pos2 - pos1);
            pos1 = pos2+1;
        }
        pos2++;
    }
    left = 0;
    while(pos1 < pos2)
        rbuf[left++] = rbuf[pos1++];
    d->peer[id].rbuf_left = left;
}

void got_connect(InstData* d, int fd)
{
    struct sockaddr saddr;
    socklen_t len = sizeof(saddr);
    int newfd = accept(fd, &saddr, &len);
    if (newfd < 0) {
        laik_panic("TCP2 Error in accept\n");
        exit(1);
    }

    add_rfd(d, newfd, got_data);

    char str[20];
    if (saddr.sa_family == AF_INET)
        inet_ntop(AF_INET, &(((struct sockaddr_in*)&saddr)->sin_addr), str, 20);
    if (saddr.sa_family == AF_INET6)
        inet_ntop(AF_INET6, &(((struct sockaddr_in6*)&saddr)->sin6_addr), str, 20);
    laik_log(1, "TCP2 Got connection on FD %d from %s\n", newfd, str);
}



//
// backend initialization
//

Laik_Instance* laik_init_tcp2(int* argc, char*** argv)
{
    char* str;

    (void) argc;
    (void) argv;

    if (instance)
        return instance;

    // my location: hostname:PID
    char location[100];
    if (gethostname(location, 100) != 0) {
        // logging not initilized yet
        fprintf(stderr, "TCP2 cannot get host name");
        exit(1);
    }
    char* host = strdup(location);
    sprintf(location + strlen(location), ":%d", getpid());

    // enable early logging
    laik_log_init_loc(location);

    // setting of home location: host/port to register with
    str = getenv("LAIK_TCP2_HOST");
    char* home_host = str ? str : "localhost";
    str = getenv("LAIK_TCP2_PORT");
    int home_port = str ? atoi(str) : 0;
    if (home_port == 0) home_port = TCP2_PORT;

    laik_log(1, "TCP2 location %s, home %s:%d\n", location, home_host, home_port);

    InstData* d = malloc(sizeof(InstData) + MAX_PEERS * sizeof(Peer));
    if (!d) {
        laik_panic("TCP2 Out of memory allocating InstData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    d->peers = 0; // zero active peers
    for(int i = 0; i < MAX_PEERS; i++) {
        d->peer[i].port = -1; // unknown peer
        d->peer[i].fd = -1;   // not connected
        d->peer[i].host = 0;
        d->peer[i].location = 0;
    }

    FD_ZERO(&d->rset);
    d->maxfds = 0;
    d->exit = 0;
    for(int i = 0; i < MAX_LOOPFD; i++)
        d->cb[i].cb = 0;

    d->host = host;
    d->location = strdup(location);
    d->listenfd = -1; // not bound yet
    d->maxid = -1;    // not set yet
    d->phase = -1;    // not set yet
    // if home host is localhost, try to become master (-1: not yet determined)
    d->id = check_local(home_host) ? 0 : -1;

    // create socket to listen for incoming TCP connections
    //  if <home_host> is not set, try to aquire local port <home_port>
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        laik_panic("TCP2 cannot create listening socket");
        exit(1); // not actually needed, laik_panic never returns
    }
    struct sockaddr_in sin;
    if (d->id == 0) {
        // mainly for development: avoid wait time to bind to same port
        if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
                       &(int){1}, sizeof(int)) < 0) {
            laik_panic("TCP2 cannot set SO_REUSEADDR");
            exit(1); // not actually needed, laik_panic never returns
        }

        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        sin.sin_port = htons(home_port);
        if (bind(listenfd, (struct sockaddr *) &sin, sizeof(sin)) < 0)
            d->id = -1; // already somebody else, still need to determine
        else
            d->listenport = home_port;
    }
    // will bind to random port if not bound yet
    if (listen(listenfd, 5) < 0) {
        laik_panic("TCP2 cannot listen on socket");
        exit(1); // not actually needed, laik_panic never returns
    }
    if (d->id < 0) {
        socklen_t len = sizeof(sin);
        if (getsockname(listenfd, (struct sockaddr *)&sin, &len) == -1) {
            laik_panic("TCP2 cannot get port of listening socket");
            exit(1); // not actually needed, laik_panic never returns
        }
        d->listenport = ntohs(sin.sin_port);
    }
    d->listenfd = listenfd;
    laik_log(1, "TCP2 listening on port %d\n", d->listenport);

    // now we know if we are master: init peer with id 0
    d->peer[0].host = (d->id == 0) ? host : home_host;
    d->peer[0].port = home_port;
    d->peer[0].location = (d->id == 0) ? d->location : 0;

    // notify us on connection requests at listening port
    add_rfd(d, d->listenfd, got_connect);

    // do registration of each non-master with master (using run-loop)
    //  newcomers block until master accepts them
    int world_size = 0; // not detected yet
    if (d->id == 0) {
        // master determines world size
        str = getenv("LAIK_SIZE");
        world_size = str ? atoi(str) : 0;
        if (world_size == 0) world_size = 1; // just master alone

        // slot 0 taken by myself
        d->maxid = 0;

        // wait for enough peers to register
        while(d->peers + 1 < world_size)
            run_loop(d);

        // send all peers to start at phase 0
        d->phase = 0;
        for(int i = 1; i <= d->maxid; i++)
            send_msg(d, i, "phase 0\n");
    }
    else {
        // register with master, get world size
        char msg[100];
        sprintf(msg, "register %.30s %.30s %d\n", location, host, d->listenport);
        send_msg(d, 0, msg);
        while(d->phase == -1)
            run_loop(d);
        world_size = d->peers + 1;
    }

    instance = laik_new_instance(&laik_backend, world_size, d->id, location, d, 0);
    laik_log(2, "TCP2 backend initialized (at '%s', rank %d/%d, listening at %d)\n",
             location, d->id, world_size, d->listenport);

    return instance;
}

void tcp2_exec(Laik_ActionSeq* as)
{
    if (as->backend == 0) {
        as->backend = &laik_backend;
        laik_aseq_calc_stats(as);
    }
    // we only support 1 transition exec action
    assert(as->actionCount == 1);
    assert(as->action[0].type == LAIK_AT_TExec);
    Laik_TransitionContext* tc = as->context[0];
    Laik_Data* d = tc->data;
    Laik_Transition* t = tc->transition;
    Laik_MappingList* fromList = tc->fromList;
    Laik_MappingList* toList = tc->toList;

    if (t->redCount > 0) {
        assert(fromList->count == 1);
        assert(toList->count == 1);
        Laik_Mapping* fromMap = &(fromList->map[0]);
        Laik_Mapping* toMap = &(toList->map[0]);
        char* fromBase = fromMap ? fromMap->base : 0;
        char* toBase = toMap ? toMap->base : 0;

        for(int i=0; i < t->redCount; i++) {
            assert(d->space->dims == 1);
            struct redTOp* op = &(t->red[i]);
            int64_t from = op->slc.from.i[0];
            int64_t to   = op->slc.to.i[0];
            assert(fromBase != 0);
            assert(laik_trans_isInGroup(t, op->outputGroup, t->group->myid));
            assert(toBase != 0);
            assert(to > from);

            laik_log(1, "TCP2 reduce: "
                        "from %lld, to %lld, elemsize %d, base from/to %p/%p\n",
                     (long long int) from, (long long int) to,
                     d->elemsize, (void*) fromBase, (void*) toBase);

            memcpy(toBase, fromBase, (to-from) * fromMap->data->elemsize);
        }
    }

    // TODO: currently no send/recv actions supported
    assert(t->recvCount == 0);
    assert(t->sendCount == 0);
}

void tcp2_sync(Laik_KVStore* kvs)
{
    // TODO
    (void) kvs;
}

#endif // USE_TCP2

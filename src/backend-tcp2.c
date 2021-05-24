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
 * Design
 *
 * The protocol used at TCP level among processes should enable easy debugging
 * and playing with ideas (eg. via 'nc'/'telnet'), performance is low priority.
 * Thus, it is based on text and line separation. Comment lines starting with
 * "# ..." are possible and ignored.
 *
 * For acceptable performance, a binary mode for data is supported but needs
 * to be announced at registration time, so it is easy to fall back to ASCII
 * with nc/telnet. Also, data packages are only accepted if permission is given
 * by receiver. This enables immediate consumption of all messages without
 * blocking.
 *
 * Startup (master)
 * - master process (location ID 0) is the process started on LAIK_TCP2_HOST
 *   (default: localhost) which successfully opens LAIK_TCP2_PORT for listening
 * - other processes (either not on LAIK_TCP2_HOST, or not able to successfully
 *   open LAIK_TCP2_PORT for listening) will connect to LAIK_TCP2_PORT on
 *   LAIK_TCP2_HOST, and after connection acceptance, they send "register"
 *   command to master
 * - master process waits for (LAIK_SIZE-1) processes to join (default for
 *   LAIK_SIZE is 1, ie. master does not wait for anybody to join) before
 *   finishing initialization and giving control back to application
 * - from the point of master, a registration uses 4 steps:
 *   (1) waiting for and accepting registration wishes, sending an "id"
 *       message to registering processes, giving each a unique location ID;
 *   (2) when enough processes registered, send all new processes info about
 *       existing ("id" line) and to all processes about newly joining
 *       ("newid" line) processes
 *   (3) request from all processes a confirmation about passed information
 *       on existing/new proceeses, and to be ready accepting direct connections;
 *   (4) telling all registered processes the entered application phase,
 *       which is 0 for startup (in contract to later joining processes).
 *   These steps are both done in startup and resize mode. However, in startup,
 *   in (2) there are no existing processes
 * - master sets application phase 0 for itself and returns control back
 *   to application.
 *
 * Notes:
 * - at registration, processes can specify a listening port for peer-to-peer
 *   transfers. However, processes only accept connections from peers they know.
 *   The registration steps make sure that after returning to the application,
 *   processes can immedately open direct connections to other processes, as
 *   these confirmed in the registration that they are ready to accept such
 *   direct connections, after step (3) - at this point, they know all involved
 *   processes
 * - when later the application of LAIK master calls into the backend again for
 *   data exchange, KVS sync, or resize requests, it also handles connection
 *   requests and commands from other processes, including requests to join by
 *   sending a register command. Registration requests will be appended to a
 *   waiting queue, with registration proceeding when LAIK calls resize().
 *
 * Registration (non-master)
 * - may start with trying to become master by opening LAIK_TCP2_PORT for
 *   listing, but this fails (if not, see startup above)
 * - open own listening port (usually randomly assigned by OS) at <myport>
 *   for later peer-to-peer connection requests and data transfers
 * - connect to master process; this may block (and time out) until master
 *   can accept connections (only if control is given to backend)
 * - send "register <mylocation> <myhost> <myport>\n" to master
 *     - <mylocation> is forwarded to LAIK as location string of joining process
 *       (can be any string, but must be unique for registration to succeed)
 *     - <myhost>/<myport> is used to allow direct connections for peer-to-peer
 *       transfer between active LAIK processes (TODO: data forwarding via master)
 * - master puts registrating process into wait queue for join wishes, and
 *   registration proceeds if either master did not yet finish startup, or when
 *   backend in master is called via resize() request to allow processes to join
 * - master sends an "id" line for the new assigned location ID of registering
 *   process "id <id> <location> <host> <port>\n"
 * - master sends further "id" lines, for each existing active process, and
 *   "newid" lines for each newly joining process.
 *   With these lists, the newly joining process can create the lists of
 *   existing and joining processes
 * - after master decides that enough processes registered, he sends "getready"
 *   to each process
 * - the registering and all existing processes sends back "ok" as response
 * - master sends compute phase and epoch to just registered process via
 *   "phase <phaseid> <epoch>\n", which allows process to start peer connections
 *   - the epoch increments for each process world change
 *   - the phase id allows new joining processes to know where to start
 *   - in startup phase, master only can do this after getting the permission
 *     from each registered process that peer connections are accepted (via "ack ok")
 * - on reception of "phase", registered processes give back control to application
 *
 * Elasticity:
 * - LAIK checks backend for processes wanting to join at compute phase change
 * - processes tell master about reached phase and ask for new IDs
 *     "enterresize <phaseid> <maxid>"
 * - master answers
 *   - new ID lines of processes joining with "newid"
 *   - (TODO) ids to be removed: "remove <id>"
 *   - request for confirmation via "getready", waiting for "ok"
 * - control given back to application, to process resize request
 *
 * Data exchange:
 * - always done directly between 2 processes, using any existing connection
 * - if no connection exists yet
 *     - receiver always waits to be connected
 *     - sender connects to listening port of receiver, sends "myid <id>\n"
 * - when receiver reaches application phase where it wants to receive the data,
 *   it give permission via "allowdata"
 * - sender sends "data <container name> <start index> <element count> <value>"
 * - connections can be used bidirectionally
 *
 * KVS Sync:
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
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
// for VSC to see def of addrinfo
#ifndef __USE_XOPEN2K
#define __USE_XOPEN2K 1
#endif
#include <netdb.h>

// defaults
#define TCP2_PORT 7777

#define MAX_PEERS 256
#define MAX_FDS 256
// receive buffer length
#define RBUF_LEN 8*1024

// forward decl
void tcp2_exec(Laik_ActionSeq* as);
void tcp2_sync(Laik_KVStore* kvs);
Laik_Group* tcp2_resize();

typedef struct _InstData InstData;

// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend = {
    .name = "Dynamic TCP2 Backend",
    .exec = tcp2_exec,
    .sync = tcp2_sync,
    .resize = tcp2_resize
};

static Laik_Instance* instance = 0;

// structs for instance

typedef enum _PeerState {
    PS_Invalid = 0,
    PS_Unknown,        // accepted connection, may be active peer or not registered
    PS_CutoffReceived, // peer/master: cutoff command received, queued
    PS_BeforeReg,      // peer/master: about to register
    PS_RegReceived,    // master peer: received registration request, in wait queue
    PS_RegAccepted,    // master peer: peer accepts config / peer: got my id, in reg
    PS_RegFinishing,   // master peer: all config sent, waiting for confirm from peer
    PS_RegFinished,    // master peer: received peer confirmation, about to make active
    PS_InStartup,      // master: in startup handshake, waiting for enough peers to join
    PS_InStartup2,     // master: enough peers joined, wait for reg handshake to finish
    PS_NoConnect,      // peer: no permission for direct connection (yet)
    PS_Ready,          // peer: ready for connect/commands/data, control may be in application
    PS_ReadyRemove,    // peer: same as ready, but marked for removal
    PS_Dead,           // peer: dead, got removed (after marked for removal)
    PS_Error,          // peer: connectivity broken
    PS_InResize,       // master/peer: in resize mode
    PS_InResize1,      // master: in resize mode, all non-masters joined
    PS_InResize2,      // master: in resize mode, waiting for peer confirmation of new info
    PS_InResize3,      // master: in resize mode, got peer confirmation of new info
    PS_InResizeRemove, // master/peer: peer marked for removal
    PS_InResizeRemove2,// master: peer marked for removal, waiting for confirmation
    PS_InResizeRemove3 // master: peer marked for removal, got confirmation
} PeerState;

// communicating peer
// can be connected (fd >=0) or not
typedef struct _Peer {
    PeerState state;
    int fd;         // -1 if no TCP connection existing to peer
    int port;       // port to connect to at host
    char* host;     // remote host, if 0 localhost
    char* location; // location string of peer

    // capabilities
    bool accepts_bin_data; // accepts binary data

    // data we are currently receiving from peer
    int rcount;    // element count in receive
    int relemsize; // expected byte count per element
    int roff;      // receive offset
    Laik_Mapping* rmap; // mapping to write received data to
    Laik_Slice* rslc; // slice to write received data to
    Laik_Index ridx; // index representing receive progress
    Laik_ReductionOperation rro; // reduction with existing value

    // allowed to send data to peer?
    int scount;    // element count allowed to send, 0 if not
    int selemsize; // byte count expected per element
} Peer;

// registrations for active fds in event loop
typedef void (*loop_cb_t)(InstData* d, int fd);
typedef struct {
    PeerState state;  // state if no LID assigned
    int lid;          // LID (location id) of peer
    loop_cb_t cb;
    char* cmd;        // unprocessed command, can be 'register' or 'remove'

    // receive buffer
    int rbuf_used;
    char* rbuf;
    // if > 0 we are in binary data receive mode, outstanding bytes
    int outstanding_bin;
} FDState;

struct _InstData {
    PeerState mystate;
    int mylid;        // my location ID
    char* host;       // my hostname
    char* location;   // my location
    int listenfd;     // file descriptor for listening to connections
    int listenport;   // port we listen at (random unless master)
    int maxid;        // highest seen id
    int phase;        // current phase
    int epoch;        // current epoch
    bool accept_bin_data; // configured to accept binary data

    // event loop
    int maxfds;       // highest fd in rset
    fd_set rset;      // read set for select
    int exit;         // set to exit event loop
    FDState fds[MAX_FDS];

    // currently synced KVS (usually NULL)
    Laik_KVStore* kvs;
    char *kvs_name;  // non-null if sending changes of KVS with given name allowed
    int kvs_changes; // number of changes expected
    int kvs_received; // counter for incoming changes

    int init_wsize;   // for master in startup: initial world size
    int peers;        // number of known peers (= valid entries in peer entry)
    int readyPeers;   // number of peers in Ready state (including ReadyRemove)
    int deadPeers;    // number of peers marked dead (still valid entry)
    Peer peer[0];
};


// helpers for send/receive of LAIK containers

// index traversal over slice
// return true if index was successfully incremented, false if traversal done
// (copied from src/layout.c)
static
bool next_lex(Laik_Slice* slc, Laik_Index* idx)
{
    idx->i[0]++;
    if (idx->i[0] < slc->to.i[0]) return true;
    if (slc->space->dims == 1) return false;

    idx->i[1]++;
    idx->i[0] = slc->from.i[0];
    if (idx->i[1] < slc->to.i[1]) return true;
    if (slc->space->dims == 2) return false;

    idx->i[2]++;
    idx->i[1] = slc->from.i[1];
    if (idx->i[2] < slc->to.i[2]) return true;
    return false;
}

static
char* istr(int dims, Laik_Index* idx)
{
    static char str[50];

    if (dims == 1)
        sprintf(str, "%llu", (unsigned long long) idx->i[0]);
    if (dims == 2)
        sprintf(str, "%llu/%llu",
                (unsigned long long) idx->i[0],
                (unsigned long long) idx->i[1]);
    if (dims == 3)
        sprintf(str, "%llu/%llu/%llu",
                (unsigned long long) idx->i[0],
                (unsigned long long) idx->i[1],
                (unsigned long long) idx->i[2]);
    return str;
}

static
char* get_statestring(PeerState st)
{
    assert(st != PS_Invalid);

    switch(st) {
    case PS_Unknown:
        // accepted connection, may be active peer or not registered
        return "unknown";

    case PS_CutoffReceived:
        // master/peer: received cutoff request, in wait queue
        return "cutoff command queued, waiting";

    case PS_BeforeReg:
        // peer/master: about to register
        return "about to register";

    case PS_RegReceived:
        // master peer: received registration request, in wait queue
        return "registration started, waiting";

    case PS_RegAccepted:
        // master peer: peer accepts config / peer: got my id, in reg
        return "registration accepted, in info exchange";

    case PS_RegFinishing:
       // master peer: all config sent, waiting for confirm from peer
       return "registration about to finish";

    case PS_RegFinished:
        // master peer: received peer confirmation, about to make active
        return "";

    case PS_InStartup:
        // master: in startup handshake, waiting for enough peers to join
        return "in startup phase, waiting for enough peers to join";

    case PS_InStartup2:
        // master: enough peers joined, wait for reg handshake to finish
        return "in startup phase, waiting for registrations to finish";

    case PS_NoConnect:
          // peer: no permission for direct connection (yet)
          return "not ready for direct connections";

    case PS_Ready:
        // peer: ready for connect/commands/data, control may be in application
        return "ready";

    case PS_ReadyRemove:
        // peer: same as ready, marked for removal
        return "ready, marked for removal";

    case PS_Dead:
        // peer: dead, got removed (after marked for removal)
        return "dead, got removed";

    case PS_Error:
        // peer: connectivity broken
        return "error, connectivity broken";

    case PS_InResize:
        // master/peer: in resize mode
        return "in resize mode";

    case PS_InResize1:
        // master/peer: in resize mode, all non-masters joined
        return "in resize mode, joined by all peers";

    case PS_InResize2:
        // master: in resize mode, waiting for peer confirmation of new info
        return "in resize mode, sent ready request to peers";

    case PS_InResize3:
        // master: in resize mode, got peer confirmation of new info
        return "in resize mode, got ready confirmation from peers";

    case PS_InResizeRemove:
        // master/peer: in resize mode, peer marked for removal
        return "in resize mode, peer marked for removal";

    case PS_InResizeRemove2:
        // master: peer marked for removal
        return "in resize mode, peer marked for removal, sent ready confirmation request";

    case PS_InResizeRemove3:
        // master: peer marked for removal
        return "in resize mode, peer marked for removal, got ready confirmation";

    default: break;
    }
    assert(0);
}


// event loop functions

void add_rfd(InstData* d, int fd, loop_cb_t cb)
{
    assert(fd < MAX_FDS);
    assert(d->fds[fd].cb == 0);

    FD_SET(fd, &d->rset);
    if (fd > d->maxfds) d->maxfds = fd;
    d->fds[fd].cb = cb;
    d->fds[fd].lid = -1;
    d->fds[fd].cmd = 0; // no unprocessed command
    d->fds[fd].rbuf = malloc(RBUF_LEN);
    d->fds[fd].rbuf_used = 0;
    d->fds[fd].outstanding_bin = 0;
}

void rm_rfd(InstData* d, int fd)
{
    assert(fd < MAX_FDS);
    assert(d->fds[fd].cb != 0);

    FD_CLR(fd, &d->rset);
    if (fd == d->maxfds)
        while(!FD_ISSET(d->maxfds, &d->rset)) d->maxfds--;
    d->fds[fd].cb = 0;
    d->fds[fd].state = PS_Invalid;
    free(d->fds[fd].rbuf);
    d->fds[fd].rbuf = 0;
}

// run event loop until an event handler asks to exit
void run_loop(InstData* d)
{
    d->exit = 0;
    while(d->exit == 0) {
        fd_set rset = d->rset;
        if (select(d->maxfds+1, &rset, 0, 0, 0) >= 0) {
            for(int i = 0; i <= d->maxfds; i++)
                if (FD_ISSET(i, &rset)) {
                    assert(d->fds[i].cb != 0);
                    (d->fds[i].cb)(d, i);
                }
        }
    }
}

// handle queued input and return immediatly
void check_loop(InstData* d)
{
    struct timeval tv = {0,0}; // zero timeout: do not block

    while(1) {
        fd_set rset = d->rset;
        int ready = select(d->maxfds+1, &rset, 0, 0, &tv);
        if (ready >= 0) {
            for(int i = 0; i <= d->maxfds; i++)
                if (FD_ISSET(i, &rset)) {
                    assert(d->fds[i].cb != 0);
                    (d->fds[i].cb)(d, i);
                }
        }
        if (ready == 0) break;
    }
}




// helper functions

// return true if hostname maps to localhost by binding a socket at arbitrary port
bool check_local(char* host)
{
    struct addrinfo hints, *info, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int ret;
    if ((ret = getaddrinfo(host, 0, &hints, &info)) != 0) {
        // host not found: not fatal here
        laik_log(1, "TCP2 check_local - host %s not found", host);
        return false;
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
void got_bytes(InstData* d, int fd);
void send_cmd(InstData* d, int lid, char* cmd);

// make sure we have an open connection to peer <lid>
// if not, connect to listening port of peer, and announce mylid
void ensure_conn(InstData* d, int lid)
{
    assert(lid < MAX_PEERS);
    if (d->peer[lid].fd >= 0) return; // connected

    if (d->peer[lid].state == PS_Error) {
        return; // cannot revive a broken connection
    }
    assert(d->peer[lid].state == PS_Ready);

    if (d->peer[lid].port < 0) {
        // we want to connect, but cannot: peer becomes broken
        d->peer[lid].state = PS_Error;
        return;
    }

    char port[20];
    sprintf(port, "%d", d->peer[lid].port);

    struct addrinfo hints, *info, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int ret;
    if ((ret = getaddrinfo(d->peer[lid].host, port, &hints, &info)) != 0) {
        laik_log(LAIK_LL_Panic, "TCP2 host %s not found - getaddrinfo %s",
                 d->peer[lid].host, gai_strerror(ret));
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
        laik_log(LAIK_LL_Warning, "TCP2 cannot connect to LID %d (host %s, port %d)",
                 lid, d->peer[lid].host, d->peer[lid].port);
        d->peer[lid].state = PS_Error;
        return;
    }

    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int)) < 0) {
        laik_panic("TCP2 cannot set TCP_NODELAY");
        exit(1); // not actually needed, laik_panic never returns
    }

    d->peer[lid].fd = fd;
    add_rfd(d, fd, got_bytes);
    d->fds[fd].lid = lid;
    laik_log(1, "TCP2 connected to LID %d (host %s, port %d)",
             lid, d->peer[lid].host, d->peer[lid].port);

    if (d->mylid >= 0) {
        // make myself known to peer: send my location id
        char msg[20];
        sprintf(msg, "myid %d", d->mylid);
        send_cmd(d, lid, msg);
    }
}

// send command <cmd> to peer <lid>
// if <lid> is negative, receiver has no LID and its the FD (as -<lid>)
//
// <cmd> can end with '\n'. When sent over wire, commands must end with '\n'.
// So if <cmd> does not end with '\n', we will send '\n' in a separate write().
// As the latter triggers Nagles algorithm for TCP (1st write without reply from
// receiver possible as partial command), common commands should end with "\n"
void send_cmd(InstData* d, int lid, char* cmd)
{
    int fd = -lid;
    if (lid >= 0) {
        ensure_conn(d, lid);
        if (d->peer[lid].state == PS_Error) {
            laik_log(1, "TCP2 Send cmd '%s' to LID %d: Cannot send, broken connection\n",
                     cmd, lid);
            return;
        }

        fd = d->peer[lid].fd;
    }
    int len = strlen(cmd);
    // pointer to NL char, set to 0 if last char is not a NL
    char* nlptr = cmd + len - 1;
    if (*nlptr != '\n') nlptr = 0;
    laik_log(1, "TCP2 Sent cmd '%s' (len %d) to LID %d (FD %d)\n",
             cmd, len, lid, fd);

    // write cmd (cope with partial writes and errors)
    int res, written = 0;
    while(written < len) {
        res = write(fd, cmd + written, len - written);
        if (res < 0) break;
        written += res;
    }
    // send NL if not last char of cmd
    if ((res >= 0) && (nlptr == 0)) while((res = write(fd, "\n", 1)) == 0);

    if (res < 0) {
        int e = errno;
        laik_log(LAIK_LL_Panic, "TCP2 write error on FD %d: %s\n",
                 fd, strerror(e));
    }
}

void send_bin(InstData* d, int lid, char* buf, int len)
{
    ensure_conn(d, lid);
    if (d->peer[lid].state == PS_Error) {
        laik_log(1, "TCP2 Send bin (len %d) to LID %d: Cannot send, broken connection\n",
                 len, lid);
        return;
    }

    int fd = d->peer[lid].fd;
    laik_log(1, "TCP2 Sent bin (len %d) to LID %d (FD %d)\n",
             len, lid, fd);

    // cope with partial writes and errors
    int res, written = 0;
    while(written < len) {
        res = write(fd, buf + written, len - written);
        if (res < 0) break;
        written += res;
    }
    if (res < 0) {
        int e = errno;
        laik_log(LAIK_LL_Panic, "TCP2 write error on FD %d: %s\n",
                 fd, strerror(e));
    }
}

int got_binary_data(InstData* d, int lid, char* buf, int len)
{
    laik_log(1, "TCP2 got binary data (from LID %d, len %d)", lid, len);

    Peer* p = &(d->peer[lid]);
    if ((p->rcount == 0) || (p->rcount == p->roff)) {
        laik_log(LAIK_LL_Warning, "TCP2 ignoring data from LID %d without send permission", lid);
        return len;
    }

    int esize = p->relemsize;
    Laik_Mapping* m = p->rmap;
    assert(m != 0);
    Laik_Layout* ll = m->layout;
    bool inTraversal = true;
    int consumed = 0;
    while(len - consumed >= esize) {
        assert(inTraversal);
        int64_t off = ll->offset(ll, m->layoutSection, &(p->ridx));
        char* idxPtr = m->start + off * p->relemsize;
        if (p->rro == LAIK_RO_None)
            memcpy(idxPtr, buf, esize);
        else {
            Laik_Type* t = p->rmap->data->type;
            assert(t->reduce);
            (t->reduce)(idxPtr, idxPtr, buf, 1, p->rro);
        }
        if ((esize == 8) && laik_log_begin(1)) {
            char pstr[70];
            int dims = p->rslc->space->dims;
            sprintf(pstr, "(%d:%s)", p->roff, istr(dims, &(p->ridx)));
            laik_log(1, " pos %s: in %f res %f\n", pstr, *((double*)buf), *((double*)idxPtr));
        }
        buf += p->relemsize;
        consumed += p->relemsize;
        p->roff++;
        inTraversal = next_lex(p->rslc, &(p->ridx));
    }
    assert(p->roff <= p->rcount);

    laik_log(1, "TCP2 consumed %d bytes, received %d/%d", consumed, p->roff, p->rcount);

    if (p->roff == p->rcount)
        d->exit = 1;

    return consumed;
}

// "data" command received
// return false if command cannot be processed yet, no matching receive
void got_data(InstData* d, int lid, char* msg)
{
    // data <len> <hexbyte> ...
    char cmd[20];
    int len, i;
    if (sscanf(msg, "%20s %d %n", cmd, &len, &i) < 2) {
        laik_log(LAIK_LL_Warning, "cannot parse data command '%s'; ignoring", msg);
        return;
    }

    Peer* p = &(d->peer[lid]);
    if ((p->rcount == 0) || (p->rcount == p->roff)) {
        laik_log(LAIK_LL_Warning, "TCP2 ignoring data from LID %d without send permission", lid);
        return;
    }

    // assume only one element per data command
    assert(p->relemsize == len);
    Laik_Mapping* m = p->rmap;
    assert(m != 0);
    Laik_Layout* ll = m->layout;
    int64_t off = ll->offset(ll, m->layoutSection, &(p->ridx));
    char* idxPtr = m->start + off * p->relemsize;

    // position string for check
    char pstr[70];
    int dims = p->rslc->space->dims;
    int s = sprintf(pstr, "(%d:%s)", p->roff, istr(dims, &(p->ridx)));

    if (msg[i] == '(') {
        assert(strncmp(msg+i, pstr, s) == 0);
        i += s;
        while(msg[i] && (msg[i] == ' ')) i++;
    }

    char data_in[100];
    assert(len < 100);
    int l = 0;

    // parse hex bytes
    unsigned char c;
    int v = 0;
    while(1) {
        c = msg[i++];
        if ((c == ' ') || (c == 0)) {
            assert(l < len);
            data_in[l++] = v;
            v = 0;
            if ((c == 0) || (l == len)) break;
            continue;
        }
        assert((c >= '0') && (c <= 'f'));
        assert((c <= '9') || (c >= 'a'));
        v = 16 * v + ((c > '9') ? c - 'a' + 10 : c - '0');
    }
    assert(l == len);

    assert(l == p->relemsize);
    if (p->rro == LAIK_RO_None)
        memcpy(idxPtr, data_in, len);
    else {
        Laik_Type* t = p->rmap->data->type;
        assert(t->reduce);
        (t->reduce)(idxPtr, idxPtr, data_in, 1, p->rro);
    }

    if (len == 8) laik_log(1, " pos %s: in %f res %f\n", pstr, *((double*)data_in), *((double*)idxPtr));

    p->roff++;
    bool inTraversal = next_lex(p->rslc, &(p->ridx));
    assert(inTraversal == (p->roff < p->rcount));

    laik_log(1, "TCP2 got data, len %d, received %d/%d",
             len, p->roff, p->rcount);

    if (p->roff == p->rcount)
        d->exit = 1;

    return;
}

void got_register(InstData* d, int fd, int lid, char* msg)
{
    // register <location> [<host> [<port> [<flags>]]]
    //  if <host> not given or set to "-": not possible to connect to this peer
    //  if <port> not given: assume default

    // ignore if not master
    if (d->mylid != 0) {
        laik_log(LAIK_LL_Warning, "ignoring register command '%s', not master", msg);
        return;
    }

    if (lid >= 0) {
        laik_log(LAIK_LL_Warning, "cannot re-register; already registered with LID %d", lid);
        return;
    }

    if ((d->mystate != PS_InStartup) &&
        (d->mystate != PS_InResize1)) {
        // after startup: process later in resize()
        assert(d->fds[fd].cmd == 0);

        d->fds[fd].state = PS_RegReceived;
        d->fds[fd].cmd = strdup(msg);
        laik_log(1, "TCP2 queued for later processing: '%s'", msg);
        return;
    }

    char cmd[20], l[50], h[50], flags[5];
    int p, res;
    res = sscanf(msg, "%20s %50s %50s %d %4s", cmd, l, h, &p, flags);
    if (res < 2) {
        laik_log(LAIK_LL_Warning, "cannot parse register command '%s'; ignoring", msg);
        return;
    }
    if (res == 2) {
        // no host and port given, set to "-", which means "not possible to reconnect"
        strcpy(h, "-");
        p = -1; // invalid
    }
    if (res == 3) {
        // no port given, assume default
        p = TCP2_PORT;
    }

    if (h[0] == '-' && h[1] == 0) {
        // host is "-": not possible to reconnect
        p = -1;
    }

    bool accepts_bin_data = false;
    if (res == 5) {
        // parse optional flags
        for(int i = 0; (i < 5) && flags[i]; i++)
            if (flags[i] == 'b') accepts_bin_data = true;
    }

    lid = ++d->maxid;
    assert(fd >= 0);
    d->fds[fd].lid = lid;
    assert(lid < MAX_PEERS);

    char loc[60];
    sprintf(loc, "L%d:%s", lid, l);

    laik_log(1, "TCP2 registered new LID %d: location %s (at host %s, port %d, flags %c)",
             lid, loc, h, p, accepts_bin_data ? 'b' : '-');

    assert(d->peer[lid].port == -1);
    d->peer[lid].state = PS_RegAccepted;
    d->peer[lid].fd = fd;
    d->peer[lid].host = strdup(h);
    d->peer[lid].location = strdup(loc);
    d->peer[lid].port = p;
    d->peer[lid].accepts_bin_data = accepts_bin_data;
    // first time we use this id for a peer: init receive
    d->peer[lid].rcount = 0;
    d->peer[lid].scount = 0;

    // send response to registering process: notify about assigned LID
    char str[150];
    sprintf(str, "id %d %s %s %d %s", lid, loc, h, p, accepts_bin_data ? "b":"-");
    send_cmd(d, lid, str);

    d->peers++;
    if (d->mystate == PS_InStartup) {
        // enough peers joined? If so, reject further join wishes
        if (d->peers + 1 == d->init_wsize)
            d->mystate = PS_InStartup2;
    }
    d->exit = 1;
}

void got_myid(InstData* d, int fd, int lid, char* msg)
{
    // myid <lid>
    // used on re-connection of other peer: peer must already be known

    char cmd[20];
    int peerid;
    if (sscanf(msg, "%20s %d", cmd, &peerid) < 2) {
        laik_log(LAIK_LL_Warning, "cannot parse myid command '%s'; ignoring", msg);
        return;
    }

    if (lid >= 0) {
        // if peer already known, id must be same
        if (lid != peerid)
            laik_log(LAIK_LL_Warning, "got ID %d from peer known by LID %d; ignoring", peerid, lid);
        return;
    }

    if (d->mylid == peerid) {
        laik_log(LAIK_LL_Warning, "got ID %d from peer which is my own LID; ignoring", peerid);
        return;
    }

    lid = peerid;
    assert((lid >= 0) && (lid < MAX_PEERS));
    assert(lid <= d->maxid);
    d->peer[lid].fd = fd;
    assert(fd >= 0);
    d->fds[fd].lid = lid;

    // must already be known, announced by master
    assert(d->peer[lid].location != 0);
    assert(d->peer[lid].host != 0);
    assert(d->peer[lid].port >= 0);

    laik_log(1, "TCP2 seen LID %d (location %s) at FD %d",
             lid, d->peer[lid].location, fd);
}

void got_cutoff(InstData* d, int fd, char* msg)
{
    // cutoff <location pattern>

    if (d->mystate != PS_InResize1) {
        assert(d->fds[fd].cmd == 0);

        d->fds[fd].state = PS_CutoffReceived;
        d->fds[fd].cmd = strdup(msg);
        laik_log(1, "TCP2 queued for later processing: '%s'", msg);
        return;
    }

    if (d->mylid > 0) {
        laik_log(LAIK_LL_Warning, "got cutoff, but not master; ignoring");
        return;
    }

    char cmd[20];
    char pattern[50];
    if (sscanf(msg, "%20s %40s", cmd, pattern) < 2) {
        laik_log(LAIK_LL_Warning, "cannot parse cutoff command '%s'; ignoring", msg);
        return;
    }

    laik_log(1, "TCP2 got cutoff, pattern '%s'", pattern);

    int rcount = 0;
    for(int lid = 1; lid <= d->maxid; lid++) {
        if (d->peer[lid].state == PS_Dead) continue;
        if (d->peer[lid].state == PS_RegAccepted) continue; // not for removal
        if (strstr(d->peer[lid].location, pattern) == 0) continue;
        assert(d->peer[lid].state == PS_InResize);
        laik_log(1, "TCP2 LID %d matched for removal", lid);
        d->peer[lid].state = PS_InResizeRemove;
        rcount++;
    }
    laik_log(1, "TCP2 marked %d processes for removal", rcount);
}

void got_help(InstData* d, int fd, int lid)
{
    // help
    laik_log(1, "TCP2 Sending usage because of help command");

    assert(fd > 0);
    if (lid == -1) lid = -fd;
    send_cmd(d, lid, "# Interactive usage (unambigous prefix is enough):");
    send_cmd(d, lid, "#  help                         : this help text");
    send_cmd(d, lid, "#  terminate                    : ask process to terminate");
    send_cmd(d, lid, "#  quit                         : close connection");
    send_cmd(d, lid, "#  status                       : request status output");
    send_cmd(d, lid, "#  cutoff <loc pattern>         : request removal of processes");
    send_cmd(d, lid, "# Protocol messages:");
    send_cmd(d, lid, "#  allowsend <count> <esize>    : give send right");
    send_cmd(d, lid, "#  data <len> [pos] <hex> ...   : data from a LAIK container");
    send_cmd(d, lid, "#  enterresize <phase> <epoch>  : enter resize phase at compute phase/epoch");
    send_cmd(d, lid, "#  getready                     : request to finish registration");
    send_cmd(d, lid, "#  id <id> <loc> <host> <port> <flags> : announce location id info");
    send_cmd(d, lid, "#  kvs allow <name>             : allow to send changes for KVS");
    send_cmd(d, lid, "#  kvs changes <count>          : announce number of changes for KVS");
    send_cmd(d, lid, "#  kvs data <key> <value>       : send changed KVS entry");
    send_cmd(d, lid, "#  myid <id>                    : identify your location id");
    send_cmd(d, lid, "#  ok                           : positive response to a request");
    send_cmd(d, lid, "#  phase <phase> <epoch>        : announce current phase/epoch");
    send_cmd(d, lid, "#  register <loc> [<host> [<port> [<flags>]]] : request assignment of id");
    send_cmd(d, lid, "# Flags:");
    send_cmd(d, lid, "#  b                            : process accepts binary data format");
}

void got_terminate(InstData* d, int fd, int lid)
{
    // terminate
    laik_log(1, "TCP2 Exiting because of terminate command");

    assert(fd > 0);
    if (lid == -1) lid = -fd;
    send_cmd(d, lid, "# Exiting. Bye");
    exit(1);
}

void got_quit(InstData* d, int fd, int lid)
{
    // quit
    laik_log(1, "TCP2 Closing connection because of quit command");

    assert(fd >= 0);
    close(fd);
    rm_rfd(d, fd);
    if (lid >= 0) d->peer[lid].fd = -1;
}

void got_status(InstData* d, int fd, int lid)
{
    // status command
    laik_log(1, "TCP2 Sending status becaue of status command");

    char msg[200];
    assert(fd > 0);
    if (lid == -1) lid = -fd;

    send_cmd(d, lid, "# Known peers:");
    for(int i = 0; i <= d->maxid; i++) {
        sprintf(msg, "#  LID%2d loc '%s' at host '%s' port %d flags %c", i,
                    d->peer[i].location, d->peer[i].host, d->peer[i].port,
                    d->peer[i].accepts_bin_data ? 'b':'-');
        send_cmd(d, lid, msg);
        if (d->peer[i].fd >= 0) {
            sprintf(msg, "#        open connection at FD %d", d->peer[i].fd);
            send_cmd(d, lid, msg);
        }
        sprintf(msg, "#        state: '%s'",
                get_statestring((i == d->mylid) ? d->mystate : d->peer[i].state));
        send_cmd(d, lid, msg);
    }
    bool header_sent = false;
    for(int i = 0; i < MAX_FDS; i++) {
        if (d->fds[i].state == PS_Invalid) continue;
        if (d->fds[i].lid >= 0) continue;
        if (!header_sent) {
            send_cmd(d, lid, "# Unknown peers:");
            header_sent = true;
        }
        sprintf(msg, "#  at FD%2d%s state '%s'", i,
                (i == fd) ? " (this connection)":"",
                get_statestring(d->fds[i].state));
        send_cmd(d, lid, msg);
        if (d->fds[i].cmd) {
            sprintf(msg, "#        queued for processing: '%s'", d->fds[i].cmd);
            send_cmd(d, lid, msg);
        }
    }
}

void got_id(InstData* d, int from_lid, char* msg)
{
    // id <lid> <location> <host> <port> <flags>
    // newid <lid> <location> <host> <port> <flags>

    // ignore if master
    if (d->mylid == 0) {
        laik_log(LAIK_LL_Warning, "ignoring id command '%s' as master", msg);
        return;
    }

    char cmd[20], l[50], h[50], flags[5];
    int lid, p;
    if (sscanf(msg, "%20s %d %50s %50s %d %4s", cmd, &lid, l, h, &p, flags) < 6) {
        laik_log(LAIK_LL_Warning, "cannot parse id command '%s'; ignoring", msg);
        return;
    }

    bool newid = (cmd[0] == 'n');

    // parse flags
    bool accepts_bin_data = false;
    for(int i = 0; (i < 5) && flags[i]; i++)
        if (flags[i] == 'b') accepts_bin_data = true;

    assert((lid >= 0) && (lid < MAX_PEERS));
    if (lid > d->maxid) d->maxid = lid;

    if (d->mylid < 0) {
        // must be response from master about accepted registration
        assert(from_lid == 0);
        d->mystate = PS_RegAccepted;
        d->mylid = lid;

        // master may have changed my location ID string
        free(d->location);
        d->location = strdup(l);

        assert(strcmp(d->host, h) == 0);
        assert(d->listenport == p);
        assert(d->accept_bin_data == accepts_bin_data);

        // copy my data also to d->peer[mylid]
        d->peer[lid].state    = d->mystate;
        d->peer[lid].host     = d->host;
        d->peer[lid].location = d->location;
        d->peer[lid].port     = d->listenport;
        d->peer[lid].accepts_bin_data = accepts_bin_data;

        laik_log(1, "TCP2 got my LID %d assigned (location %s, at %s, port %d, flags %c)",
             lid, l, h, p, accepts_bin_data ? 'b':'-');
        return;
    }

    // must be information about another peer
    assert(lid != d->mylid);
    // should not get same information twice
    assert(d->peer[lid].location == 0);

    // set peer state depending on own state
    switch(d->mystate) {
        case PS_RegAccepted:
            // in newcomer, announced process is:
            // - (with "id") existing process in resize mode
            // - (with "newid") newcomer, not allowed to connect yet
            d->peer[lid].state = newid ? PS_NoConnect : PS_InResize;
            break;
        case PS_InResize:
            // in existing process: this is a new-comer, no connect yet
            d->peer[lid].state = PS_NoConnect;
            assert(newid); // only new ids announced to existing processes
            break;
        default:
            laik_panic("Got id in wrong phase");
    }
    d->peer[lid].host = strdup(h);
    d->peer[lid].location = strdup(l);
    d->peer[lid].port = p;
    d->peer[lid].accepts_bin_data = accepts_bin_data;

    // first time we see this peer: init receive
    d->peer[lid].rcount = 0;
    d->peer[lid].scount = 0;

    d->peers++;

    laik_log(1, "TCP2 seen peer LID %d (location %s, at %s, port %d, flags %c), known peers %d",
             lid, l, h, p, accepts_bin_data ? 'b':'-', d->peers);
}

void got_phase(InstData* d, char* msg)
{
    // phase <phase> <epoch>

    // ignore if master
    if (d->mylid == 0) {
        laik_log(LAIK_LL_Warning, "ignoring phase command '%s' as master", msg);
        return;
    }

    char cmd[20];
    int phase, epoch;
    if (sscanf(msg, "%20s %d %d", cmd, &phase, &epoch) < 3) {
        laik_log(LAIK_LL_Warning, "cannot parse phase command '%s'; ignoring", msg);
        return;
    }
    laik_log(1, "TCP2 got phase %d / epoch %d", phase, epoch);
    d->phase = phase;
    d->epoch = epoch;

    d->exit = 1;
}

void got_enterresize(InstData* d, int lid, char* msg)
{
    // enterresize [<phase> [<epoch>]]
    char cmd[20];
    int phase, epoch;
    int res = sscanf(msg, "%20s %d %d", cmd, &phase, &epoch);
    if (res < 1) {
        laik_log(LAIK_LL_Warning, "cannot parse enterresize command '%s'; ignoring", msg);
        return;
    }
    laik_log(1, "TCP2 got info that LID %d is in resize mode", lid);

    if (res < 2) {
        // no phase given, assume own
        phase = instance->phase;
    }
    if (res < 3) {
        // no epoch given, assume own
        epoch = instance->epoch;
    }

    assert((lid >= 0) && (lid < MAX_PEERS));
    assert(d->peer[lid].state == PS_Ready);
    assert(instance->phase == phase);
    assert(instance->epoch == epoch);
    d->peer[lid].state = PS_InResize;

    d->exit = 1;
}

void got_backedout(InstData* d, int lid, char* msg)
{
    // backedout <lid>
    char cmd[20];
    int backedout_lid;
    if (sscanf(msg, "%20s %d", cmd, &backedout_lid) < 2) {
        laik_log(LAIK_LL_Warning, "cannot parse backedout command '%s'; ignoring", msg);
        return;
    }

    if (lid >0) {
        laik_log(LAIK_LL_Warning, "got backedout cmd from non-master; ignoring");
        return;
    }

    laik_log(1, "TCP2 got backedout for LID %d", backedout_lid);

    switch(d->mystate) {
    case PS_RegAccepted:
    case PS_InResize:
    case PS_InResizeRemove:
        break;
    default:
        laik_log(LAIK_LL_Panic, "got backedout cmd not in resize/regaccept");
        return;
    }

    assert((backedout_lid > 0) && (backedout_lid <= d->maxid));
    // in non-master, other processes may still be marked as Ready (even though in resize)
    assert((d->peer[backedout_lid].state == PS_InResize) ||
           (d->peer[backedout_lid].state == PS_Ready));
    d->peer[backedout_lid].state = PS_InResizeRemove;
    if (d->mylid == backedout_lid)
        d->mystate = PS_InResizeRemove;
}

void got_allowsend(InstData* d, int lid, char* msg)
{
    // allowsend <count> <elemsize>
    char cmd[20];
    int count, esize;
    if (sscanf(msg, "%20s %d %d", cmd, &count, &esize) < 3) {
        laik_log(LAIK_LL_Warning, "cannot parse allowsend command '%s'; ignoring", msg);
        return;
    }

    laik_log(1, "TCP2 got allowsend %d %d", count, esize);
    if (d->peer[lid].scount != 0) {
        laik_log(LAIK_LL_Warning, "already have send right; ignoring");
        return;
    }

    d->peer[lid].scount = count;
    d->peer[lid].selemsize = esize;
    d->exit = 1;
}

void got_kvs_allow(InstData* d, int lid, char* msg)
{
    if (lid != 0) {
        laik_log(LAIK_LL_Warning, "ignoring 'kvs allow' from LID %d", lid);
        return;
    }

    char cmd[20];
    char name[30];
    if (sscanf(msg, "%20s %29s", cmd, name) < 2) {
        laik_log(LAIK_LL_Warning, "cannot parse 'kvs allow' command '%s'; ignoring", msg);
        return;
    }

    laik_log(1, "TCP2 allowed to send changes for KVS '%s'", name);
    assert(d->kvs_name == 0);
    d->kvs_name = strdup(name);
    d->exit = 1;
}

void got_kvs_changes(InstData* d, int lid, char* msg)
{
    char cmd[20];
    int changes = 0;
    if (sscanf(msg, "%20s %d", cmd, &changes) < 2) {
        laik_log(LAIK_LL_Warning, "cannot parse 'kvs changes' command '%s'; ignoring", msg);
        return;
    }

    laik_log(1, "TCP2 got %d changes announced for KVS '%s' from LID %d",
             changes, d->kvs->name, lid);
    assert(d->kvs_changes == -1);
    assert(d->kvs_received == 0);
    d->kvs_changes = changes;
    if (changes == 0)
        d->exit = 1;
}

void got_kvs_data(InstData* d, int lid, char* msg)
{
    char cmd[20], key[30], value[110];
    if (sscanf(msg, "%20s %29s %100[^\n]", cmd, key, value) < 3) {
        laik_log(LAIK_LL_Warning, "cannot parse kvs data command '%s'; ignoring", msg);
        return;
    }

    laik_log(1, "TCP2 got KVS data from LID %d for key '%s': '%s'", lid, key, value);
    assert(d->kvs != 0);
    assert(d->kvs_changes > 0);
    assert(d->kvs_received < d->kvs_changes);

    Laik_KVS_Entry* e = laik_kvs_sets(d->kvs, key, value);
    // simply mark it as updated
    //  this may send unneeded updates, but we do not use change journal in TCP2
    e->updated = true;

    d->kvs_received++;
    if (d->kvs_received == d->kvs_changes)
        d->exit = 1;
}


void got_kvs(InstData* d, int lid, char* msg)
{
    // kvs ...
    msg++;
    if (*msg == 'v') msg++;
    if (*msg == 's') msg++;
    if (*msg == ' ') msg++;

    switch(*msg) {
    case 'a': got_kvs_allow(d, lid, msg); break;
    case 'c': got_kvs_changes(d, lid, msg); break;
    case 'd': got_kvs_data(d, lid, msg); break;
    default:
        laik_log(LAIK_LL_Warning, "cannot parse kvs command '%s'; ignoring", msg);
        break;
    }
}

void got_getready(InstData* d, int lid, char* msg)
{
    if (lid != 0) {
        laik_log(LAIK_LL_Warning, "ignoring 'getready' from LID %d", lid);
        return;
    }

    char cmd[20];
    if (sscanf(msg, "%15s", cmd) < 1) {
        laik_log(LAIK_LL_Warning, "cannot parse 'getready' command '%s'; ignoring", msg);
        return;
    }

    PeerState newstate = PS_Invalid;
    switch(d->mystate) {
    case PS_RegAccepted:
        laik_log(1, "TCP2 got 'getready' from LID %d during registration", lid);
        newstate = PS_Ready;
        break;
    case PS_InResize:
        laik_log(1, "TCP2 got 'getready' from LID %d during resize", lid);
        newstate = PS_Ready;
        break;
    case PS_InResizeRemove:
        laik_log(1, "TCP2 got 'getready' from LID %d during resize, marked for removal", lid);
        newstate = PS_ReadyRemove;
        break;
    default:
        laik_log(LAIK_LL_Warning, "ignoring 'getready', already ready");
        return;
    }

    sprintf(cmd, "ok");
    send_cmd(d, lid, cmd);
    d->mystate = newstate;
    d->peer[d->mylid].state = newstate;

    d->exit = 1;
}

void got_ok(InstData* d, int lid, char* msg)
{
    char cmd[20];
    if (sscanf(msg, "%15s", cmd) < 1) {
        laik_log(LAIK_LL_Warning, "cannot parse 'ok' command '%s'; ignoring", msg);
        return;
    }

    if ((d->mylid == 0) && (d->peer[lid].state == PS_RegFinishing)) {
        laik_log(1, "TCP2 got 'ok' from LID %d: registration done", lid);

        d->peer[lid].state = PS_Ready;
        d->readyPeers++;
        if (d->mystate == PS_InStartup2) {
            // finished all registrations?
            if (d->readyPeers + 1 == d->init_wsize)
                d->mystate = PS_Ready;
        }
        d->exit = 1;
        return;
    }

    if ((d->mylid == 0) &&
        ((d->peer[lid].state == PS_InResize2) ||
         (d->peer[lid].state == PS_InResizeRemove2))) {
        laik_log(1, "TCP2 got 'ok' from LID %d: resize changes accepted", lid);

        if (d->peer[lid].state == PS_InResizeRemove2)
            d->peer[lid].state = PS_InResizeRemove3;
        else
            d->peer[lid].state = PS_InResize3;
        d->exit = 1;
        return;
    }

    laik_log(LAIK_LL_Warning, "ignoring 'ok' from LID %d", lid);
}

// a command was received from a peer, and should be processed
// return false if command cannot be processed yet
//   this only happens with "data" command without matching receive
void got_cmd(InstData* d, int fd, char* msg, int len)
{
    int lid = d->fds[fd].lid;
    laik_log(1, "TCP2 Got cmd '%s' (len %d) from LID %d (FD %d)\n",
            msg, len, lid, fd);
    if (len == 0) return;

    // first part of commands are accepted without assigned ID
    switch(msg[0]) {
    case 'r': got_register(d, fd, lid, msg); return; // register <location> <host> <port>
    case 'm': got_myid(d, fd, lid, msg); return; // myid <lid>
    case 'c': got_cutoff(d, fd, msg); return; // cutoff <location pattern>
    case 'h': got_help(d, fd, lid); return;
    case 't': got_terminate(d, fd, lid); return;
    case 'q': got_quit(d, fd, lid); return;
    case 's': got_status(d, fd, lid); return;
    case '#': return; // # - comment, ignore
    default: break;
    }

    // ignore if sender unknown (only register allowed from yet-unknown sender)
    if (lid < 0) {
        laik_log(LAIK_LL_Warning, "ignoring command '%s' from unknown sender", msg);
        assert(fd > 0);
        if (lid == -1) lid = -fd;
        send_cmd(d, lid, "# first register, see 'help'");
        return;
    }

    // second part of commands are accepted only with ID assigned by master
    switch(msg[0]) {
    case 'i': got_id(d, lid, msg); return; // id <lid> <location> <host> <port>
    case 'n': got_id(d, lid, msg); return; // newid <lid> <location> <host> <port>
    case 'e': got_enterresize(d, lid, msg); return; // enterresize <phase> <epoch>
    case 'b': got_backedout(d, lid, msg); return; // backedout <lid>
    case 'p': got_phase(d, msg); return; // phase <phaseid>
    case 'a': got_allowsend(d, lid, msg); return; // allowsend <count> <elemsize>
    case 'd': got_data(d, lid, msg); return; // data <len> [(<pos>)] <hex> ...
    case 'k': got_kvs(d, lid, msg); return; // kvs ...
    case 'g': got_getready(d, lid, msg); return; // getready
    case 'o': got_ok(d, lid, msg); return; // ok
    default: break;
    }

    laik_log(LAIK_LL_Warning, "TCP2 got from LID %d unknown msg '%s'", lid, msg);
}

void process_rbuf(InstData* d, int fd)
{
    assert((fd >= 0) && (fd < MAX_FDS));
    FDState* fds = &(d->fds[fd]);
    char* rbuf = fds->rbuf;
    int used = fds->rbuf_used;
    int outstanding_bin = fds->outstanding_bin;
    assert(rbuf != 0);

    laik_log(1, "TCP2 handle commands in receive buf of FD %d (LID %d, %d bytes)\n",
             fd, fds->lid, used);

    int consumed;
    // pos1/pos2: start/end of section to process
    int pos1 = 0, pos2 = 0;
    while(pos2 < used) {
        // section in bin mode?
        if (outstanding_bin > 0) {
            if (used - pos1 < outstanding_bin) {
                // all bytes in receive buffer are in bin mode
                consumed = got_binary_data(d, fds->lid, rbuf + pos1, used - pos1);
                if (consumed == 0) {
                    // may happen if available chunk too small, need more data
                    pos2 = used;
                    break;
                }
            }
            else {
                consumed = got_binary_data(d, fds->lid, rbuf + pos1, outstanding_bin);
                assert(consumed > 0); // we provided all bytes until end, ensure progress
            }
            outstanding_bin -= consumed;
            pos1 += consumed;
            pos2 = pos1;
            continue;
        }
        // start of bin mode?
        if (rbuf[pos1] == 'B') {
            // 3 bytes header: 'B' + 2 bytes count (up to 64k of binary)
            if (pos1 + 2 >= used) {
                // not enough bytes to cover header: stop
                pos2 = used;
                break;
            }
            outstanding_bin  = ((int)((unsigned char*)rbuf)[pos1 + 1]);
            outstanding_bin += ((int)((unsigned char*)rbuf)[pos1 + 2]) << 8;
            laik_log(1, "TCP2 bin mode started with %d bytes\n", outstanding_bin);
            pos1 += 3;
            pos2 = pos1;
            continue;
        }

        if (rbuf[pos2] == 4) { // Ctrl+D: same as "quit"
            got_cmd(d, fd, "quit", 5);
            pos1 = pos2+1;
        }
        if (rbuf[pos2] == 13) {
            // change CR to whitespace (sent by telnet)
            rbuf[pos2] = ' ';
        }
        if (rbuf[pos2] == '\n') {
            rbuf[pos2] = 0;
            got_cmd(d, fd, rbuf + pos1, pos2 - pos1);
            pos1 = pos2+1;
        }
        pos2++;
    }
    if (pos1 > 0) {
        used = 0;
        while(pos1 < pos2)
            rbuf[used++] = rbuf[pos1++];
    }
    fds->rbuf_used = used;
    fds->outstanding_bin = outstanding_bin;
}

void got_bytes(InstData* d, int fd)
{
    // use a per-fd receive buffer to not mix partially sent commands
    assert((fd >= 0) && (fd < MAX_FDS));
    int used = d->fds[fd].rbuf_used;

    if (used == RBUF_LEN) {
        // buffer not large enough for even 1 command: should not happen
        laik_panic("TCP2 receive buffer too small for 1 command");
        exit(1);
    }

    char* rbuf = d->fds[fd].rbuf;
    int len = read(fd, rbuf + used, RBUF_LEN - used);
    if (len == -1) {
        int e = errno;
        laik_log(1, "TCP2 warning: read error on FD %d: %s\n",
                 fd, strerror(e));
        return;
    }
    if (len == 0) {
        // other side closed connection

        if (used > 0) {
            // process left-over commands, add NL for last command to process
            rbuf[used] = '\n';
            d->fds[fd].rbuf_used++;
            process_rbuf(d, fd);
        }

        int lid = d->fds[fd].lid;
        laik_log(1, "TCP2 FD %d closed (peer LID %d, %d bytes unprocessed)\n",
                 fd, lid, d->fds[fd].rbuf_used);

        if (lid >= 0) {
            assert(d->peer[lid].fd == fd);
            // peer may still be alive and just have closed connection to avoid
            // too many open connections: thus, only mark as "not connected"
            d->peer[lid].fd = -1;
        }

        close(fd);
        rm_rfd(d, fd);
        return;
    }

    if (laik_log_begin(1)) {
        char lstr[100];
        int i, o;
        o = sprintf(lstr, "%02x", (unsigned char) rbuf[used]);
        for(i = 1; (i < len) && (i < 8); i++)
            o += sprintf(lstr + o, " %02x", (unsigned char) rbuf[used + i]);
        if (i < len) sprintf(lstr + o, "...");
        assert(o < 100);
        laik_log_flush("TCP2 got_bytes(FD %d, peer LID %d, used %d): read %d bytes (%s)\n",
                       fd, d->fds[fd].lid, used, len, lstr);
    }

    d->fds[fd].rbuf_used = used + len;
    process_rbuf(d, fd);
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

    add_rfd(d, newfd, got_bytes);
    d->fds[newfd].state = PS_Unknown;

    char str[20];
    if (saddr.sa_family == AF_INET)
        inet_ntop(AF_INET, &(((struct sockaddr_in*)&saddr)->sin_addr), str, 20);
    if (saddr.sa_family == AF_INET6)
        inet_ntop(AF_INET6, &(((struct sockaddr_in6*)&saddr)->sin6_addr), str, 20);
    laik_log(1, "TCP2 Got connection on FD %d from %s\n", newfd, str);

    char msg[100];
    sprintf(msg, "# Here is LAIK TCP2 LID %d (type 'help' for commands)", d->mylid);
    send_cmd(d, -newfd, msg);
}



//---------------------------------------------------------------------------
// backend initialization
//

// helper for backend init
static
InstData* new_inst_data(char* host, char* location)
{
    InstData* d = malloc(sizeof(InstData) + MAX_PEERS * sizeof(Peer));
    if (!d) {
        laik_panic("TCP2 Out of memory allocating InstData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    d->init_wsize = -1; // unknown initial world size
    d->peers = 0; // zero known peers
    d->readyPeers = 0; // zero ready peers
    d->deadPeers = 0;
    for(int i = 0; i < MAX_PEERS; i++) {
        d->peer[i].state = PS_Invalid;
        d->peer[i].port = -1; // unknown peer
        d->peer[i].fd = -1;   // not connected
        d->peer[i].host = 0;
        d->peer[i].location = 0;
        d->peer[i].accepts_bin_data = false;
        d->peer[i].rcount = 0;
        d->peer[i].scount = 0;
    }

    FD_ZERO(&d->rset);
    d->maxfds = 0;
    d->exit = 0;
    for(int i = 0; i < MAX_FDS; i++) {
        d->fds[i].state = PS_Invalid;
        d->fds[i].cb = 0;
    }

    d->host = strdup(host);
    d->location = strdup(location);
    d->listenfd = -1; // not bound yet
    d->maxid = -1;    // not set yet
    d->phase = -1;    // not set yet
    d->epoch = -1;    // not set yet
    d->mylid = -1;    // net yet determined
    // announce capability to accept binary data? Defaults to yes, can be switched off
    char* str = getenv("LAIK_TCP2_BIN");
    d->accept_bin_data = str ? atoi(str) : 1;
    d->kvs = 0;       // only set during tcp2_sync()
    d->kvs_changes = 0;
    d->kvs_received = 0;
    d->kvs_name = 0;

    return d;
}

// startup handshake of master
// returns world size
static
int startup_master(InstData* d)
{
    // master determines world size
    char* str = getenv("LAIK_SIZE");
    int world_size = str ? atoi(str) : 0;
    if (world_size == 0) world_size = 1; // just master alone

    // slot 0 taken by myself
    d->maxid = 0;
    // we start in phase 0, epoch 0
    d->phase = 0;
    d->epoch = 0;

    if (world_size == 1) return 1;

    // handshake with non-masters

    laik_log(1, "TCP2 master: waiting for %d peers to join\n", world_size - 1);
    // wait for enough peers to register
    d->init_wsize = world_size;
    while(d->mystate == PS_InStartup)
        run_loop(d);
    assert(d->peers + 1 == world_size);
    assert(d->mystate == PS_InStartup2);

    // broadcast location ID infos to all non-masters
    char msg[150];
    for(int lid = 0; lid <= d->maxid; lid++) {
        sprintf(msg, "newid %d %s %s %d %s", lid,
                d->peer[lid].location, d->peer[lid].host, d->peer[lid].port,
                d->peer[lid].accepts_bin_data ? "b":"-");
        for(int to_lid = 1; to_lid <= d->maxid; to_lid++) {
            if (lid == to_lid) continue;
            send_cmd(d, to_lid, msg);
        }
    }

    // notify peers to get ready, and wait for them to become ready
    // (ready means they accept direct connections)
    for(int i = 1; i <= d->maxid; i++) {
        assert(d->peer[i].state == PS_RegAccepted);
        d->peer[i].state = PS_RegFinishing;
        send_cmd(d, i, "getready");
    }
    while(d->mystate == PS_InStartup2)
        run_loop(d);
    assert(d->readyPeers + 1 == world_size);

    laik_log(1, "TCP2 master: %d peers registered, startup done\n", d->readyPeers);

    // notify all peers to start at phase 0, epoch 0
    for(int i = 1; i <= d->maxid; i++) {
        assert(d->peer[i].state == PS_Ready);
        send_cmd(d, i, "phase 0 0");
    }

    return world_size;
}

// returns world size
static
int startup_non_master(InstData* d)
{
    // register with master, get world size
    char msg[100];
    sprintf(msg, "register %.30s %.30s %d %s",
            d->location, d->host, d->listenport, d->accept_bin_data ? "bin":"");
    send_cmd(d, 0, msg);

    // wait until "getready" from master, confirmed with "ok", setting myself to ready
    while(d->mystate != PS_Ready)
        run_loop(d);

    // wait for active phase
    while(d->phase == -1)
        run_loop(d);

    // LIDs without any info received actually are dead
    int dead = 0;
    for(int i = 0; i <= d->maxid; i++)
        if (d->peer[i].state == PS_Invalid) {
            d->peer[i].state = PS_Dead;
            dead++;
        }
    d->deadPeers = dead;

    return d->peers + 1;
}


Laik_Instance* laik_init_tcp2(int* argc, char*** argv)
{
    char* str;

    (void) argc;
    (void) argv;

    if (instance)
        return instance;

    // this is required to not get spurious SIGPIPE signals
    // from opened sockets, e.g. if other side closes connection
    // TODO: LAIK as library should not touch global signal handlers...
    signal(SIGPIPE, SIG_IGN);

    // my location string: "<hostname>:<pid>" (may be extended by master)
    char hostname[50];
    if (gethostname(hostname, 50) != 0) {
        // logging not initilized yet
        fprintf(stderr, "TCP2 cannot get host name");
        exit(1);
    }
    char location[70];
    sprintf(location, "%s:%d", hostname, getpid());

    // enable early logging
    laik_log_init_loc(location);
    if (laik_log_begin(1)) {
        laik_log_append("TCP2 init: cmdline '%s", (*argv)[0]);
        for(int i = 1; i < *argc; i++)
            laik_log_append(" %s", (*argv)[i]);
        laik_log_flush("'\n");
    }

    // setting of home location: host/port to register with
    str = getenv("LAIK_TCP2_HOST");
    char* home_host = str ? str : "localhost";
    str = getenv("LAIK_TCP2_PORT");
    int home_port = str ? atoi(str) : 0;
    if (home_port == 0) home_port = TCP2_PORT;

    laik_log(1, "TCP2 location '%s', home %s:%d\n", location, home_host, home_port);

    InstData* d = new_inst_data(hostname, location);

    //
    // create listening socket and determine who is master
    //

    // if home host is localhost, try to become master
    bool try_master = check_local(home_host);

    // create socket to listen for incoming TCP connections
    //  if <home_host> is not set, try to aquire local port <home_port>
    // we may need to try creating the listening socket twice
    struct sockaddr_in sin;
    int listenfd = -1;
    while(1) {
        listenfd = socket(PF_INET, SOCK_STREAM, 0);
        if (listenfd < 0) {
            laik_panic("TCP2 cannot create listening socket");
            exit(1); // not actually needed, laik_panic never returns
        }
        if (try_master) {
            // mainly for development: avoid wait time to bind to same port
            if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
                           &(int){1}, sizeof(int)) < 0) {
                laik_panic("TCP2 cannot set SO_REUSEADDR");
                exit(1); // not actually needed, laik_panic never returns
            }

            sin.sin_family = AF_INET;
            sin.sin_addr.s_addr = htonl(INADDR_ANY);
            sin.sin_port = htons(home_port);
            if (bind(listenfd, (struct sockaddr *) &sin, sizeof(sin)) == 0) {
                // listen on successfully bound socket
                // if this fails, another process started listening first
                // and we need to open another socket, as we cannot unbind
                if (listen(listenfd, 5) < 0) {
                    laik_log(1,"listen failed, opening new socket");
                    close(listenfd);
                    continue;
                }
                // we successfully became master: my LID is 0
                d->mylid = 0;
                d->listenport = home_port;
                break;
            }
        }
        // not bound yet: will bind to random port
        if (listen(listenfd, 5) < 0) {
            laik_panic("TCP2 cannot listen on socket");
            exit(1); // not actually needed, laik_panic never returns
        }
        break;
    }

    if (d->mylid < 0) {
        socklen_t len = sizeof(sin);
        if (getsockname(listenfd, (struct sockaddr *)&sin, &len) == -1) {
            laik_panic("TCP2 cannot get port of listening socket");
            exit(1); // not actually needed, laik_panic never returns
        }
        d->listenport = ntohs(sin.sin_port);
    }
    d->listenfd = listenfd;

    // notify us on connection requests at listening port
    add_rfd(d, d->listenfd, got_connect);

    laik_log(1, "TCP2 listening on port %d\n", d->listenport);

    // now we know if we are master: init peer with id 0
    if (d->mylid == 0) {
        // we are master
        d->mystate = PS_InStartup;

        // add LID tag to my location
        char loc[100];
        sprintf(loc, "L0:%s", d->location);
        free(d->location);
        d->location = strdup(loc);

        // copy my data also to d->peer[0]
        d->peer[0].state    = d->mystate;
        d->peer[0].host     = d->host;
        d->peer[0].location = d->location;
        d->peer[0].port     = d->listenport;
        d->peer[0].accepts_bin_data = d->accept_bin_data;
    }
    else {
        // we are non-master: we want to register with master
        d->mystate = PS_BeforeReg;
        d->peer[0].state = PS_Ready; // we assume master to accept reg requests
        // only to be able to connect to master, will be updated from master
        d->peer[0].host = home_host;
        d->peer[0].port = home_port;
    }


    //
    // run startup protocol handshake: non-masters register with master
    //

    int world_size;
    if (d->mylid == 0) {
        world_size = startup_master(d);
        // set all peers to PS_NoConnect, to count as newly added
        for(int i = 0; i <= d->maxid; i++)
            d->peer[i].state = PS_NoConnect;
    }
    else {
        world_size = startup_non_master(d);
        // set myself to PS_NoConnect: newly added
        d->peer[d->mylid].state = PS_NoConnect;
    }

    //
    // finished initialization: we are ready
    //

    instance = laik_new_instance(&laik_backend, d->maxid + 1, d->mylid,
                                 d->epoch, d->phase, d->location, d);

    int added = 0, old = 0, remove = 0;
    for(int i = 0; i <= d->maxid; i++) {
        if (d->peer[i].state == PS_Dead) continue;
        if (d->peer[i].state == PS_NoConnect) added++;
        else if (d->peer[i].state == PS_InResize) old++;
        else if (d->peer[i].state == PS_InResizeRemove) { old++; remove++; }
        else assert(0);
    }
    laik_log(1, "TCP2 newcomer: added %d, old %d, remove %d (dead %d)",
             added, old, remove, d->deadPeers);
    assert(world_size == old + added);
    world_size -= remove;

    // create initial world group
    Laik_Group* world = laik_create_group(instance, world_size);
    world->size = world_size;
    if (old == 0) {
        // we are part of initial processes: no parent
        // location IDs are process IDs in initial world
        world->myid = d->mylid;
        for(int i = 0; i < world_size; i++) {
            world->locationid[i] = i;
            d->peer[i].state = PS_Ready;
        }
    }
    else {
        Laik_Group* parent = laik_create_group(instance, old);
        parent->size = old;
        parent->myid = -1; // not in parent group
        int parentID = 0, worldID = 0;
        for(int lid = 0; lid <= d->maxid; lid++) {
            if (d->peer[lid].state == PS_Dead) continue;
            if (d->mylid == lid) world->myid = worldID;
            if (d->peer[lid].state == PS_InResize) {
                // in old, also in new group
                d->peer[lid].state = PS_Ready;
                parent->locationid[parentID] = lid;
                world->locationid[worldID] = lid;
                world->toParent[worldID] = parentID;
                world->fromParent[parentID] = worldID;
                worldID++;
                parentID++;
                continue;
            }
            if (d->peer[lid].state == PS_InResizeRemove) {
                // only in old group
                d->peer[lid].state = PS_ReadyRemove;
                parent->locationid[parentID] = lid;
                world->fromParent[parentID] = -1; // does not exist in new group
                parentID++;
                continue;
            }
            // only in new group
            assert(d->peer[lid].state == PS_NoConnect);
            d->peer[lid].state = PS_Ready;
            world->locationid[worldID] = lid;
            world->toParent[worldID] = -1;
            worldID++;
        }
        assert(parentID == old);
        assert(worldID == world_size);
        world->parent = parent;
    }
    // attach world to instance
    instance->world = world;

    d->mystate = PS_Ready;

    laik_log(2, "TCP2 backend initialized (location '%s', LID %d, rank %d/%d, epoch %d, phase %d, listening at %d, flags: %c)\n",
             d->location, d->mylid, world->myid, world_size,
             d->epoch, d->phase, d->listenport, d->accept_bin_data ? 'b':'-');

    return instance;
}


// helper for exec

// send data with one element of size <s> at pointer <p> to process <lid>
// position <n/idx> added only to allow check at receiver
static
void send_data(int n, int dims, Laik_Index* idx, int toLID, void* p, int s)
{
    char str[100];
    int o = 0;
    o += sprintf(str, "data %d (%d:%s)", s, n, istr(dims, idx));
    for(int i = 0; i < s; i++) {
        int v = ((unsigned char*)p)[i];
        o += sprintf(str+o, " %02x", v);
    }

    if (laik_log_begin(1)) {
        laik_log_append("TCP2 %d bytes data to LID %d", s, toLID);
        if (s == 8)
            laik_log_flush(", pos (%d:%s): %f\n", n, istr(dims, idx), *((double*)p));
        else
            laik_log_flush("");
    }

    send_cmd((InstData*)instance->backend_data, toLID, str);
}

// send

#define SBUF_LEN 8*1024
int sbuf_used = 3; // reserve space for header
int sbuf_toLID = -1;
char sbuf[SBUF_LEN];

static
void send_data_bin_flush(int toLID)
{
    if (sbuf_used == 0) return;
    assert(sbuf_toLID == toLID);

    // prepend data to send with header with byte count
    int bytes = sbuf_used - 3;
    sbuf[0] = 'B';
    sbuf[1] = bytes & 255;
    sbuf[2] = bytes >> 8;
    send_bin((InstData*)instance->backend_data, toLID, sbuf, sbuf_used);
    sbuf_used = 3; // reserve space for header
    sbuf_toLID = -1;
}

static
void send_data_bin(int n, int dims, Laik_Index* idx, int toLID, void* p, int s)
{
    if (sbuf_used + s > SBUF_LEN)
        send_data_bin_flush(toLID);
    if (sbuf_toLID < 0)
        sbuf_toLID = toLID;
    else
        assert(sbuf_toLID == toLID);

    memcpy(sbuf + sbuf_used, p, s);
    sbuf_used += s;

    if (laik_log_begin(1)) {
        laik_log_append("TCP2 add %d bytes bin data to LID %d", s, toLID);
        if (s == 8)
            laik_log_flush(", pos (%d:%s): %f\n", n, istr(dims, idx), *((double*)p));
        else
            laik_log_flush("");
    }
}


// send a slice of data from mapping <m> to process <lid>
// if not yet allowed to send data, we have to wait.
// the action sequence ordering makes sure that there must
// be a matching receive action on the receiver side
static
void send_slice(Laik_Mapping* fromMap, Laik_Slice* slc, int toLID)
{
    Laik_Layout* l = fromMap->layout;
    int esize = fromMap->data->elemsize;
    int dims = slc->space->dims;
    assert(fromMap->start != 0); // must be backed by memory

    InstData* d = (InstData*)instance->backend_data;
    Peer* p = &(d->peer[toLID]);
    if (p->scount == 0) {
        // we need to wait for right to send data
        while(p->scount == 0)
            run_loop(d);
    }
    assert(p->scount == (int) laik_slice_size(slc));
    assert(p->selemsize == esize);

    bool send_binary_data = p->accepts_bin_data;
    Laik_Index idx = slc->from;
    int ecount = 0;
    while(1) {
        int64_t off = l->offset(l, fromMap->layoutSection, &idx);
        void* idxPtr = fromMap->start + off * esize;
        if (send_binary_data)
            send_data_bin(ecount, dims, &idx, toLID, idxPtr, esize);
        else
            send_data(ecount, dims, &idx, toLID, idxPtr, esize);
        ecount++;
        if (!next_lex(slc, &idx)) break;
    }
    assert(ecount == (int) laik_slice_size(slc));
    if (send_binary_data)
        send_data_bin_flush(toLID);

    // withdraw our right to send further data
    p->scount = 0;
}

// queue receive action and run event loop until all data received
// <ro> allows to request reduction with existing value
// (use RO_None to overwrite with received value)
static
void recv_slice(Laik_Slice* slc, int fromLID, Laik_Mapping* toMap, Laik_ReductionOperation ro)
{
    assert(toMap->start != 0); // must be backed by memory
    // check no data still registered to be received from <fromID>
    InstData* d = (InstData*)instance->backend_data;
    Peer* p = &(d->peer[fromLID]);
    assert(p->rcount == 0);

    // write outstanding receive info into peer structure
    p->rcount = laik_slice_size(slc);
    assert(p->rcount > 0);
    p->roff = 0;
    p->relemsize = toMap->data->elemsize;
    p->rmap = toMap;
    p->rslc = slc;
    p->ridx = slc->from;
    p->rro = ro;

    // give peer the right to start sending data consisting of given number of elements
    char msg[50];
    sprintf(msg, "allowsend %d %d\n", p->rcount, p->relemsize);
    send_cmd(d, fromLID, msg);

    // wait until all data received from peer
    while(p->roff < p->rcount)
        run_loop(d);

    // done
    p->rcount = 0;
}

/* reduction at one process using send/recv
 * 
 * One process is chosen to do the reduction (reduceProcess): this is selected
 * to be the process with smallest id of all processes which are interested in the
 * result (input group). All other processes with input send their data to the
 * reduceProcess. It does the reduction, and then it sends the result to all
 * processes interested in the result (output group)
*/
static
void exec_reduce(Laik_TransitionContext* tc,
                 Laik_BackendAction* a)
{
    assert(a->h.type == LAIK_AT_MapGroupReduce);
    Laik_Transition* t = tc->transition;

    // do the manual reduction on smallest rank of output group
    int reduceTask = laik_trans_taskInGroup(t, a->outputGroup, 0);
    int reduceLID = laik_group_locationid(t->group, reduceTask);
    laik_log(1, "  reduce process is T%d (LID %d)", reduceTask, reduceLID);

    int myid = t->group->myid;
    if (myid != reduceTask) {
        // not the reduce process: eventually send input and recv result

        if (laik_trans_isInGroup(t, a->inputGroup, myid)) {
            laik_log(1, "  not reduce process: send to T%d (LID %d)",
                     reduceTask, reduceLID);
            assert(tc->fromList && (a->fromMapNo < tc->fromList->count));
            Laik_Mapping* m = &(tc->fromList->map[a->fromMapNo]);
            send_slice(m, a->slc, reduceLID);
        }
        if (laik_trans_isInGroup(t, a->outputGroup, myid)) {
            laik_log(1, "  not reduce process: recv from T%d (LID%d)",
                     reduceTask, reduceLID);
            assert(tc->toList && (a->toMapNo < tc->toList->count));
            Laik_Mapping* m = &(tc->toList->map[a->toMapNo]);
            recv_slice(a->slc, reduceLID, m, LAIK_RO_None);
        }
        return;
    }

    // this is the reduce process

    assert(tc->toList && (a->toMapNo < tc->toList->count));
    Laik_Mapping* m = &(tc->toList->map[a->toMapNo]);

    // do receive & reduce with all input processes
    Laik_ReductionOperation op = a->redOp;
    if (!laik_trans_isInGroup(t, a->inputGroup, myid)) {
        // no input from me: overwrite my values
        op = LAIK_RO_None;
    }
    else {
        // input from me: if from different map, copy to output map
        assert(tc->fromList && (a->fromMapNo < tc->fromList->count));
        Laik_Mapping* fromMap = &(tc->fromList->map[a->fromMapNo]);
        if (fromMap != m) {
            // copy
            laik_data_copy(a->slc, fromMap, m);
        }
    }
    int inCount = laik_trans_groupCount(t, a->inputGroup);
    for(int i = 0; i < inCount; i++) {
        int inTask = laik_trans_taskInGroup(t, a->inputGroup, i);
        if (inTask == myid) continue;
        int inLID = laik_group_locationid(t->group, inTask);

        laik_log(1, "  reduce process: recv + %s from T%d (LID %d), count %d",
                 (op == LAIK_RO_None) ? "overwrite":"reduce", inTask, inLID, a->count);
        recv_slice(a->slc, inLID, m, op);
        op = a->redOp; // eventually reset to reduction op from None
    }

    // send result to processes in output group
    int outCount = laik_trans_groupCount(t, a->outputGroup);
    for(int i = 0; i< outCount; i++) {
        int outTask = laik_trans_taskInGroup(t, a->outputGroup, i);
        if (outTask == myid) {
            // that's myself: nothing to do
            continue;
        }
        int outLID = laik_group_locationid(t->group, outTask);

        laik_log(1, "  reduce process: send result to T%d (LID %d)", outTask, outLID);
        send_slice(m, a->slc, outLID);
    }
}


void tcp2_exec(Laik_ActionSeq* as)
{
    if (as->actionCount == 0) {
        laik_log(1, "TCP2 exec: nothing to do\n");
        return;
    }

    if (as->backend == 0) {
        as->backend = &laik_backend;

        // do minimal transformations, sorting send/recv
        laik_log(1, "TCP2 exec: prepare before exec\n");
        laik_log_ActionSeqIfChanged(true, as, "Original sequence");
        bool changed = laik_aseq_splitTransitionExecs(as);
        laik_log_ActionSeqIfChanged(changed, as, "After splitting texecs");
        changed = laik_aseq_sort_2phases(as);
        laik_log_ActionSeqIfChanged(changed, as, "After sorting");

        laik_aseq_calc_stats(as);
        as->backend = 0; // this tells LAIK that no cleanup needed
    }

    Laik_TransitionContext* tc = as->context[0];
    Laik_Action* a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        switch(a->type) {
        case LAIK_AT_MapPackAndSend: {
            Laik_A_MapPackAndSend* aa = (Laik_A_MapPackAndSend*) a;
            int toLID = laik_group_locationid(tc->transition->group, aa->to_rank);
            laik_log(1, "TCP2 MapPackAndSend to T%d (LID %d), %d x %dB\n",
                     aa->to_rank, toLID, aa->count, tc->data->elemsize);
            assert(tc->fromList && (aa->fromMapNo < tc->fromList->count));
            Laik_Mapping* m = &(tc->fromList->map[aa->fromMapNo]);
            send_slice(m, aa->slc, toLID);
            break;
        }
        case LAIK_AT_MapRecvAndUnpack: {
            Laik_A_MapRecvAndUnpack* aa = (Laik_A_MapRecvAndUnpack*) a;
            int fromLID = laik_group_locationid(tc->transition->group, aa->from_rank);
            laik_log(1, "TCP2 MapRecvAndUnpack from T%d (LID %d), %d x %dB\n",
                     aa->from_rank, fromLID, aa->count, tc->data->elemsize);
            assert(tc->toList && (aa->toMapNo < tc->toList->count));
            Laik_Mapping* m = &(tc->toList->map[aa->toMapNo]);
            recv_slice(aa->slc, fromLID, m, LAIK_RO_None);
            break;
        }

        case LAIK_AT_MapGroupReduce: {
            Laik_BackendAction* aa = (Laik_BackendAction*) a;
            laik_log(1, "TCP2 MapGroupReduce %d x %dB\n",
                     aa->count, tc->data->elemsize);
            exec_reduce(tc, aa);
            break;
        }

        default:
            assert(0);
            break;
        }
    }
}

void tcp2_sync(Laik_KVStore* kvs)
{
    char msg[100];
    InstData* d = (InstData*)instance->backend_data;

    int count = 0;
    for(unsigned int i = 0; i < kvs->used; i++) {
        if (!kvs->entry[i].updated) continue;
        count++;
    }
    laik_log(1, "TCP2 syncing KVS '%s' with %d own changes", kvs->name, count);

    // must not be in middle of another sync
    assert(d->kvs == 0);
    // if we already have permission to send data, it must be for this KVS
    if (d->kvs_name)
        assert(strcmp(d->kvs_name, kvs->name) == 0);
    d->kvs = kvs;

    if (d->mylid > 0) {
        laik_log(1, "TCP2 waiting for allowance to send changes");
        while(d->kvs_name == 0)
            run_loop(d);
        // this must be allowance to send changes for same KVS
        assert(strcmp(kvs->name, d->kvs_name) == 0);

        sprintf(msg, "kvs changes %d", count);
        send_cmd(d, 0, msg);
        for(unsigned int i = 0; i < kvs->used; i++) {
            if (!kvs->entry[i].updated) continue;
            sprintf(msg, "kvs data %s %s",
                    kvs->entry[i].key, kvs->entry[i].value);
            send_cmd(d, 0, msg);
        }
        // all changes sent, remove own permission
        // (needs to be done here, as we may receive next allowance
        //  before end of sync, which would trigger assertion)
        free(d->kvs_name);
        d->kvs_name = 0;
        // wait for all changes being send by LID 0
        d->kvs_changes = -1;
        d->kvs_received = 0;
        while((d->kvs_changes < 0) || (d->kvs_received < d->kvs_changes))
            run_loop(d);
        laik_log(1, "TCP2 synced %d changes for KVS %s",
                d->kvs_changes, kvs->name);
        d->kvs = 0;
        return;
    }

    // master

    for(int lid = 1; lid <= d->maxid; lid++) {
        sprintf(msg, "kvs allow %s", kvs->name);
        send_cmd(d, lid, msg);

        // wait for changes from LID <lid>
        d->kvs_changes = -1;
        d->kvs_received = 0;
        while((d->kvs_changes < 0) || (d->kvs_received < d->kvs_changes))
            run_loop(d);
        laik_log(1, "TCP2 got %d changes for KVS %s from LID %d",
                d->kvs_changes, kvs->name, lid);
    }
    count = 0;
    for(unsigned int i = 0; i < kvs->used; i++) {
        if (!kvs->entry[i].updated) continue;
        count++;
    }
    laik_log(1, "TCP2 with %d merged changes", count);
    sprintf(msg, "kvs changes %d", count);
    for(int lid = 1; lid <= d->maxid; lid++)
        send_cmd(d, lid, msg);
    for(unsigned int i = 0; i < kvs->used; i++) {
        if (!kvs->entry[i].updated) continue;
        sprintf(msg, "kvs data %s %s",
                kvs->entry[i].key, kvs->entry[i].value);
        for(int lid = 1; lid <= d->maxid; lid++)
            send_cmd(d, lid, msg);
    }
    laik_log(1, "TCP2 synced %d changes for KVS %s",
            count, kvs->name);
    d->kvs = 0;
}

// return new group on process size change (global sync)
Laik_Group* tcp2_resize()
{
    char msg[150];
    InstData* d = (InstData*)instance->backend_data;
    if (d->mystate == PS_ReadyRemove) {
        // cannot join in resize, outside of current world
        return 0;
    }
    assert(d->mystate == PS_Ready);
    d->peer[d->mylid].state = PS_InResize;
    d->mystate = PS_InResize;

    int phase = instance->phase;
    int epoch = instance->epoch;
    laik_log(1, "TCP2 resize: phase %d, epoch %d", phase, epoch);

    // peers marked for removal are now dead
    int markedDead = 0;
    for(int lid = 1; lid <= d->maxid; lid++)
        if (d->peer[lid].state == PS_ReadyRemove) {
            d->peer[lid].state = PS_Dead;
            markedDead++;
        }
    d->deadPeers += markedDead;
    d->readyPeers -= markedDead;

    if (d->mylid > 0) {
        // tell master that we are in resize mode
        sprintf(msg, "enterresize %d %d", phase, epoch);
        send_cmd(d, 0, msg);

        // wait for master to finish resize phase
        d->phase = -1;
        while(d->phase != phase)
            run_loop(d);

        int added = 0, to_remove = 0;
        for(int lid = 0; lid <= d->maxid; lid++) {
            switch(d->peer[lid].state) {
            case PS_NoConnect:
                added++; break;
            case PS_InResizeRemove: // other process
            case PS_ReadyRemove: // this process, ready
                to_remove++; break;
            case PS_Dead:
            case PS_Ready:
                break;
            default: assert(0);
            }
        }

        if ((added == 0) && (to_remove == 0)) {
            // nothing changed
            laik_log(1, "TCP2 resize: nothing changed");
            assert(d->peer[d->mylid].state == PS_Ready);
            assert(d->mystate == PS_Ready);
            return 0;
        }

        // create new group from current world group, with parent relatinship
        Laik_Group* w = instance->world;
        Laik_Group* g = laik_create_group(instance, d->maxid + 1);
        g->parent = w;
        int i1 = 0, i2 = 0; // i1: index in parent, i2: new process index
        for(int lid = 0; lid <= d->maxid; lid++) {
            switch(d->peer[lid].state) {
            case PS_Dead: break;
            case PS_Ready:
                // both in old and new group
                assert(w->locationid[i1] == lid);
                g->locationid[i2] = lid;
                g->toParent[i2] = i1;
                g->fromParent[i1] = i2;
                i1++;
                i2++;
                break;
            case PS_NoConnect:
                // new registered
                d->peer[lid].state = PS_Ready;
                g->locationid[i2] = lid;
                g->toParent[i2] = -1; // did not exist before
                i2++;
                break;
            case PS_InResizeRemove: // other process
            case PS_ReadyRemove:
                // marked for removal
                d->peer[lid].state = PS_ReadyRemove;
                assert(w->locationid[i1] == lid);
                g->fromParent[i1] = -1; // does not exist in new group
                i1++;
                break;
            default:
                assert(0);
            }
        }
        assert(w->size == i1);
        g->size = i2;
        g->myid = g->fromParent[w->myid];
        instance->locations = d->maxid + 1;

        laik_log(1, "TCP2 resize: locations %d (added %d, to remove %d), new group (size %d, my id %d)",
                 instance->locations, added, to_remove, g->size, g->myid);
        d->mystate = d->peer[d->mylid].state;
        return g;
    }

    // master

    // process incoming commands
    check_loop(d);

    // wait for all ready processes to join resize phase
    for(int lid = 1; lid <= d->maxid; lid++) {
        if (d->peer[lid].state != PS_Ready) continue;
        while(d->peer[lid].state == PS_Ready)
            run_loop(d);
        // originally "ready" processes may become broken: simply skip them
        if (d->peer[lid].state == PS_Error) continue;
        assert(d->peer[lid].state == PS_InResize);
    }

    d->mystate = PS_InResize1;

    // process queued join / remove requests
    for(int fd = 0; fd < MAX_FDS; fd++) {
        if (d->fds[fd].state == PS_Invalid) continue;
        if (d->fds[fd].lid >= 0) continue;
        if (!d->fds[fd].cmd) continue;

        laik_log(1, "TCP2 resize: replay '%s' from FD %d",
                 d->fds[fd].cmd, fd);
        got_cmd(d, fd, d->fds[fd].cmd, strlen(d->fds[fd].cmd));
        free(d->fds[fd].cmd);
        d->fds[fd].cmd = 0;
    }

    // check how many new processes got accepted / are marked for removal
    int added = 0, to_remove = 0;
    for(int i = 1; i <= d->maxid; i++) {
        if (d->peer[i].state == PS_RegAccepted) added++;
        else if (d->peer[i].state == PS_InResizeRemove) to_remove++;
    }

    // broadcast location ID info of old processes to all new-comers
    for(int lid = 0; lid <= d->maxid; lid++) {
        if (d->peer[lid].state == PS_Dead) continue;
        if (d->peer[lid].state == PS_RegAccepted) continue;
        // <lid> is an old process
        sprintf(msg, "id %d %s %s %d %s", lid,
                d->peer[lid].location, d->peer[lid].host, d->peer[lid].port,
                d->peer[lid].accepts_bin_data ? "b":"-");
        for(int to_lid = 1; to_lid <= d->maxid; to_lid++) {
            if (d->peer[to_lid].state != PS_RegAccepted) continue;
            assert(lid != to_lid);
            send_cmd(d, to_lid, msg);
        }
    }

    // broadcast location ID info of all new-comers to all non-masters
    for(int lid = 0; lid <= d->maxid; lid++) {
        if (d->peer[lid].state != PS_RegAccepted) continue;
        sprintf(msg, "newid %d %s %s %d %s", lid,
                d->peer[lid].location, d->peer[lid].host, d->peer[lid].port,
                d->peer[lid].accepts_bin_data ? "b":"-");
        for(int to_lid = 1; to_lid <= d->maxid; to_lid++) {
            if (d->peer[to_lid].state == PS_Dead) continue;
            if (lid == to_lid) continue;
            send_cmd(d, to_lid, msg);
        }
    }

    // broadcast LIDs of all processes marked for removal
    for(int lid = 0; lid <= d->maxid; lid++) {
        if (d->peer[lid].state != PS_InResizeRemove) continue;
        sprintf(msg, "backedout %d", lid);
        for(int to_lid = 1; to_lid <= d->maxid; to_lid++) {
            if (d->peer[to_lid].state == PS_Dead) continue;
            send_cmd(d, to_lid, msg);
        }
    }

    // request confirmation from all non-master about new info: send 'getReady'
    for(int i = 1; i <= d->maxid; i++) {
        if (d->peer[i].state == PS_Dead) continue;
        if (d->peer[i].state == PS_RegAccepted)
            d->peer[i].state = PS_RegFinishing;
        else if (d->peer[i].state == PS_InResize)
            d->peer[i].state = PS_InResize2;
        else if (d->peer[i].state == PS_InResizeRemove)
            d->peer[i].state = PS_InResizeRemove2;
        else
            assert(0); // should not happen

        send_cmd(d, i, "getready");
    }

    // wait for ready confirmation
    int ready = 0, dead = 0;
    for(int lid = 1; lid <= d->maxid; lid++) {
        if (d->peer[lid].state == PS_Dead) { dead++; continue; }
        // go on if ready (was joining), in resize3 (stays), in remove3 (about to leave)
        while((d->peer[lid].state != PS_Ready) &&
              (d->peer[lid].state != PS_InResize3) &&
              (d->peer[lid].state != PS_InResizeRemove3)) {
            run_loop(d);
        }
        ready++;
    }
    assert(ready == d->readyPeers);
    assert(dead == d->deadPeers);

    laik_log(1, "TCP2 resize master: %d ready peers (%d added, %d to remove), %d dead",
             ready, added, to_remove, dead);

    // finish resize
    if ((added > 0) || (to_remove > 0)) epoch++;
    sprintf(msg, "phase %d %d", phase, epoch);
    for(int lid = 1; lid <= d->maxid; lid++) {
        if (d->peer[lid].state == PS_Dead) continue;
        send_cmd(d, lid, msg);
    }

    if ((added == 0) && (to_remove == 0)) {
        // nothing changed
        for(int lid = 1; lid <= d->maxid; lid++) {
            if (d->peer[lid].state == PS_Dead) continue;
            d->peer[lid].state = PS_Ready;
        }
        d->mystate = PS_Ready;
        return 0;
    }

    // create new group from current world group, with parent relatinship
    Laik_Group* w = instance->world;
    Laik_Group* g = laik_create_group(instance, d->maxid + 1);
    g->parent = w;
    int i1 = 0, i2 = 0; // i1: index in parent, i2: new process index
    for(int lid = 0; lid <= d->maxid; lid++) {
        if (d->peer[lid].state == PS_Dead) continue;
        if ((d->peer[lid].state == PS_InResize) ||
            (d->peer[lid].state == PS_InResize3)) {
            // both in old and new group
            d->peer[lid].state = PS_Ready;
            assert(w->locationid[i1] == lid);
            g->locationid[i2] = lid;
            g->toParent[i2] = i1;
            g->fromParent[i1] = i2;
            i1++;
            i2++;
            continue;
        }
        if (d->peer[lid].state == PS_Ready) {
            // new registered
            g->locationid[i2] = lid;
            g->toParent[i2] = -1; // did not exist before
            i2++;
            continue;
        }
        if (d->peer[lid].state == PS_InResizeRemove3) {
            // marked for removal
            d->peer[lid].state = PS_ReadyRemove;
            assert(w->locationid[i1] == lid);
            g->fromParent[i1] = -1; // does not exist in new group
            i1++;
            continue;
        }
        assert(0);
    }
    assert(w->size == i1);
    g->size = i2;
    g->myid = g->fromParent[w->myid];
    instance->locations = d->maxid + 1;
    assert(d->deadPeers == dead);

    laik_log(1, "TCP2 resize master: locations %d (%d ready, %d dead), new group (size %d, my id %d)",
             instance->locations, ready, dead, g->size, g->myid);
    d->mystate = PS_Ready;
    return g;
}

#endif // USE_TCP2

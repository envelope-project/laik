/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// default log level
static Laik_LogLevel laik_loglevel = LAIK_LL_Error;
static FILE* laik_logfile = NULL;
static Laik_Instance* laik_loginst = 0;
// filter
static int laik_log_fromtask = -1;
static int laik_log_totask = -1;

//program name
extern const char *__progname;

int laik_size(Laik_Group* g)
{
    return g->size;
}

int laik_myid(Laik_Group* g)
{
    return g->myid;
}

void laik_finalize(Laik_Instance* inst)
{
    laik_log(1, "finalizing...");
    if (inst->backend && inst->backend->finalize)
        (*inst->backend->finalize)(inst);

    if (inst->repart_ctrl){
        laik_ext_cleanup(inst);
    }

    if (laik_log_begin(2)) {
        laik_log_append("switch statistics (this task):\n");
        for(int i=0; i<inst->data_count; i++) {
            Laik_Data* d = inst->data[i];
            laik_log_append("  data '%s': ", d->name);
            laik_log_SwitchStat(d->stat);
        }
        laik_log_flush(0);
    }

    laik_close_profiling_file(inst);
    if(laik_logfile){
        fclose(laik_logfile);
    }
    
    free(inst->control);
    laik_free_profiling(inst);
}

// return a backend-dependant string for the location of the calling task
char* laik_mylocation(Laik_Instance* inst)
{
    return inst->mylocation;
}

// allocate space for a new LAIK instance
Laik_Instance* laik_new_instance(Laik_Backend* b,
                                 int size, int myid,
                                 char* location, void* data)
{
    Laik_Instance* instance;
    instance = malloc(sizeof(Laik_Instance));
    if (!instance) {
        laik_panic("Out of memory allocating Laik_Instance object");
        exit(1); // not actually needed, laik_panic never returns
    }

    instance->backend = b;
    instance->backend_data = data;
    instance->size = size;
    instance->myid = myid;
    instance->mylocation = strdup(location);

    instance->firstSpaceForInstance = 0;

    instance->kvstore = laik_kv_newNode(0, 0, 0); // empty root
    instance->group_count = 0;
    instance->data_count = 0;
    instance->mapping_count = 0;

    laik_space_init();
    laik_data_init();
    instance->control = laik_program_control_init();
    instance->profiling = laik_init_profiling();

    instance->repart_ctrl = 0;

    // logging (TODO: multiple instances)
    laik_loginst = instance;
    char* str = getenv("LAIK_LOG");
    if (str) {
        int l = atoi(str);
        if (l > 0)
            laik_loglevel = l;
        char* p = index(str, ':');
        if (p) {
            p++;
            laik_log_fromtask = atoi(p);
            p = index(p, '-');
            if (p) {
                p++;
                laik_log_totask = atoi(p);
            }
            else
                laik_log_totask = laik_log_fromtask;
        }
    }

    str = getenv("LAIK_LOG_FILE");
    if(str){
        laik_logfile = freopen(str, "a+", stdout);
        if(!laik_logfile){
            laik_log(LAIK_LL_Error, "Cannot Initialize File for print output.\n");
        }
        stderr = laik_logfile;
        stdout = laik_logfile;
    }

    return instance;
}

// add/remove space to/from instance
void laik_addSpaceForInstance(Laik_Instance* inst, Laik_Space* s)
{
    assert(s->nextSpaceForInstance == 0);
    s->nextSpaceForInstance = inst->firstSpaceForInstance;
    inst->firstSpaceForInstance = s;
}

void laik_removeSpaceFromInstance(Laik_Instance* inst, Laik_Space* s)
{
    if (inst->firstSpaceForInstance == s) {
        inst->firstSpaceForInstance = s->nextSpaceForInstance;
    }
    else {
        // search for previous item
        Laik_Space* ss = inst->firstSpaceForInstance;
        while(ss->nextSpaceForInstance != s)
            ss = ss->nextSpaceForInstance;
        assert(ss != 0); // not found, should not happen
        ss->nextSpaceForInstance = s->nextSpaceForInstance;
    }
    s->nextSpaceForInstance = 0;
}

void laik_addDataForInstance(Laik_Instance* inst, Laik_Data* d)
{
    assert(inst->data_count < MAX_DATAS);
    inst->data[inst->data_count] = d;
    inst->data_count++;
}


// create a group to be used in this LAIK instance
Laik_Group* laik_create_group(Laik_Instance* i)
{
    assert(i->group_count < MAX_GROUPS);

    Laik_Group* g;

    g = malloc(sizeof(Laik_Group) + 2 * (i->size) * sizeof(int));
    if (!g) {
        laik_panic("Out of memory allocating Laik_Group object");
        exit(1); // not actually needed, laik_panic never returns
    }
    i->group[i->group_count] = g;

    g->inst = i;
    g->gid = i->group_count;
    g->size = 0; // yet invalid
    g->backend_data = 0;
    g->parent = 0;
    g->firstPartitioningForGroup = 0;

    // space after struct
    g->toParent   = (int*) (((char*)g) + sizeof(Laik_Group));
    g->fromParent = g->toParent + i->size;

    i->group_count++;
    return g;
}

void laik_addPartitioningForGroup(Laik_Group* g, Laik_Partitioning* p)
{
    assert(p->nextPartitioningForGroup == 0);
    p->nextPartitioningForGroup = g->firstPartitioningForGroup;
    g->firstPartitioningForGroup = p;
}

void laik_removePartitioningFromGroup(Laik_Group* g, Laik_Partitioning* p)
{
    if (g->firstPartitioningForGroup == p) {
        g->firstPartitioningForGroup = p->nextPartitioningForGroup;
    }
    else {
        // search for previous item
        Laik_Partitioning* pp = g->firstPartitioningForGroup;
        while(pp->nextPartitioningForGroup != p)
            pp = pp->nextPartitioningForGroup;
        assert(pp != 0); // not found, should not happen
        pp->nextPartitioningForGroup = p->nextPartitioningForGroup;
    }
    p->nextPartitioningForGroup = 0;
}

Laik_Group* laik_world(Laik_Instance* i)
{
    // world must have been added by backend
    assert(i->group_count > 0);

    Laik_Group* g = i->group[0];
    assert(g->gid == 0);
    assert(g->inst == i);
    assert(g->size == i->size);

    return g;
}

// create a clone of <g>, derived from <g>.
Laik_Group* laik_clone_group(Laik_Group* g)
{
    Laik_Group* g2 = laik_create_group(g->inst);
    g2->parent = g;
    g2->size = g->size;
    g2->myid = g->myid;

    for(int i=0; i < g->size; i++) {
        g2->toParent[i] = i;
        g2->fromParent[i] = i;
    }

    assert(g2->firstPartitioningForGroup == 0); // still empty

    return g2;
}


// Shrinking (collective)
Laik_Group* laik_new_shrinked_group(Laik_Group* g, int len, int* list)
{
    Laik_Group* g2 = laik_clone_group(g);

    for(int i = 0; i < g->size; i++)
        g2->fromParent[i] = 0; // init

    for(int i = 0; i < len; i++) {
        assert((list[i] >= 0) && (list[i] < g->size));
        g2->fromParent[list[i]] = -1; // mark removed
    }
    int o = 0;
    for(int i = 0; i < g->size; i++) {
        if (g2->fromParent[i] < 0) continue;
        g2->fromParent[i] = o;
        g2->toParent[o] = i;
        o++;
    }
    g2->size = o;
    g2->myid = (g->myid < 0) ? -1 : g2->fromParent[g->myid];

    if (g->inst->backend->updateGroup)
        (g->inst->backend->updateGroup)(g2);

    if (laik_log_begin(1)) {
        laik_log_append("shrink group: "
                        "%d (size %d, myid %d) => %d (size %d, myid %d):",
                        g->gid, g->size, g->myid, g2->gid, g2->size, g2->myid);
        laik_log_append("\n  fromParent (to shrinked)  : ");
        laik_log_IntList(g->size, g2->fromParent);
        laik_log_append("\n  toParent   (from shrinked): ");
        laik_log_IntList(g2->size, g2->toParent);
        laik_log_flush(0);
    }

    return g2;
}

// Utilities

char* laik_get_guid(Laik_Instance* i){
    return i->guid;
}

// Logging



// to overwrite environment variable LAIK_LOG
void laik_set_loglevel(Laik_LogLevel l)
{
    laik_loglevel = l;
}

// check for log level: return true if given log level will be shown
bool laik_logshown(Laik_LogLevel l)
{
    return (l >= laik_loglevel);
}

/* Log a message, similar to printf
 *
 * A prefix is added which allows sorting to get stable output
 * from the arbitrarily interleaved output of multiple MPI tasks:
 *
 * == LAIK-<phasectr>.<iter> T<task>/<tasks> <phasemsgctr>.<line> <pname>
 *
 * <phasectr>    a counter incremented on every phase change
 * <iter>        iteration counter set by application
 * <task>        task rank in this LAIK instance
 * <phasemsgctr> log message counter, reset at each phase change
 * <pname>       phase name set by application
 *
 * To build the message step by step:
 * - start: laik_log_begin(<level>)
 * - optionally multiple times: laik_log_append(<msg>, ...)
 * - end with laik_log_flush(<msg>, ...)
 *
 * Or just use log(<level>, <msg>, ...) which internally uses above functions
*/

// buffered logging, not thread-safe

static Laik_LogLevel current_logLevel = LAIK_LL_None;
static char* current_logBuffer = 0;
static int current_logSize = 0;
static int current_logPos = 0;

bool laik_log_begin(Laik_LogLevel l)
{
    // if nothing should be logged, set level to none and return
    if (l < laik_loglevel) {
        current_logLevel = LAIK_LL_None;
        return false;
    }
    if (laik_log_fromtask >= 0) {
        assert(laik_loginst != 0);
        assert(laik_log_totask >= laik_log_fromtask);
        if ((laik_loginst->myid < laik_log_fromtask) ||
            (laik_loginst->myid > laik_log_totask)) {
            current_logLevel = LAIK_LL_None;
            return false;
        }
    }
    current_logLevel = l;

    current_logPos = 0;
    if (current_logBuffer == 0) {
        // init: start with 1k buffer
        current_logBuffer = malloc(1024);
        assert(current_logBuffer); // cannot call laik_panic
        current_logSize = 1024;
    }
    return true;
}

static
void log_append(const char *format, va_list ap)
{
    if (current_logLevel == LAIK_LL_None) return;

    // to be able to do a 2nd pass over ap (if buffer is too small)
    va_list ap2;
    va_copy(ap2, ap);

    int left, len;
    left = current_logSize - current_logPos;
    assert(left > 0);
    len = vsnprintf(current_logBuffer + current_logPos, left,
                    format, ap);

    // does it fit into buffer? (len is without terminating zero byte)
    if (len >= left) {
        int size = 2 * current_logSize;
        if (size < len + 1) size = len + 1;
        current_logBuffer = realloc(current_logBuffer, size);
        current_logSize = size;
        // printf("Enlarging log buffer to %d bytes ...\n", size);

        // print again into enlarged buffer - must fit
        left = current_logSize - current_logPos;
        len = vsnprintf(current_logBuffer + current_logPos, left,
                                   format, ap2);
        assert(len < left);
    }

    current_logPos += len;
}

void laik_log_append(const char* msg, ...)
{
    if (current_logLevel == LAIK_LL_None) return;

    va_list args;
    va_start(args, msg);
    log_append(msg, args);
}

static
void log_flush()
{
    if (current_logLevel == LAIK_LL_None) return;
    if ((current_logPos == 0) || (current_logBuffer == 0)) return;

    const char* lstr = 0;
    switch(current_logLevel) {
        case LAIK_LL_Warning: lstr = "Warning"; break;
        case LAIK_LL_Error:   lstr = "ERROR"; break;
        case LAIK_LL_Panic:   lstr = "PANIC"; break;
        default: break;
    }

    // counters for stable output
    static int counter = 0;
    static int last_phase_counter = 0;
    int line_counter = 0;
    assert(laik_loginst != 0);
    if (last_phase_counter != laik_loginst->control->phase_counter) {
        counter = 0;
        last_phase_counter = laik_loginst->control->phase_counter;
    }
    counter++;

#define LINE_LEN 100
    // enough for prefix plus one line of log message
    static char buf2[100 + LINE_LEN];
    int off1 = 0, off, off2;

    char* buf1 = current_logBuffer;

    const char* phase = laik_loginst->control->cur_phase_name;
    if (!phase) phase = "";
    int spaces = 0, last_break = 0;
    bool at_newline = true;

    // append prefix at beginning of each line of msg
    while(buf1[off1]) {

        // prefix to allow sorting of log output
        // sorting makes chunks from output of each MPI task
        line_counter++;
        off2 = sprintf(buf2,
                       "== LAIK-%03d.%02d T%2d/%d %04d.%02d %-10s ",
                       laik_loginst->control->phase_counter,
                       laik_loginst->control->cur_iteration,
                       laik_loginst->myid, laik_loginst->size,
                       counter, line_counter, phase);
        if (lstr)
                off2 += sprintf(buf2+off2, "%-7s: ",
                                (line_counter == 1) ? lstr : "");

        // line of message

        if (at_newline) {
            // get indent
            spaces = 0;
            while(buf1[off1] == ' ') { off1++; spaces++; }
        }

        // indent: add 4 spaces if this is continuation line
        off2 += sprintf(buf2+off2, "%*s",
                        at_newline ? spaces : spaces + 4, "");

        at_newline = false;
        off = off1;

        last_break = 0;
        while(buf1[off]) {
            if (buf1[off] == '\n') {
                at_newline = true;
                break;
            }
            if (buf1[off] == ' ') {
                // break line if too long?
                if (spaces + (off - off1) > LINE_LEN) {
                    if (last_break)
                        off = last_break; // go back
                    break;
                }
                last_break = off;
            }
            off++;
        }
        if (buf1[off]) buf1[off++] = 0;
        off2 += sprintf(buf2+off2, "%s\n", buf1 + off1);
        off1 = off;

        assert(off2 < 100 + LINE_LEN);

        // TODO: allow to go to debug file
        fprintf(stderr, "%s", buf2);
    }

    // stop program on panic with failed assertion
    if (current_logLevel == LAIK_LL_Panic) assert(0);
}

void laik_log_flush(const char* msg, ...)
{
    if (current_logLevel == LAIK_LL_None) return;

    if (msg) {
        va_list args;
        va_start(args, msg);
        log_append(msg, args);
    }

    log_flush();
}

void laik_log(Laik_LogLevel l, const char* msg, ...)
{
    if (!laik_log_begin(l)) return;

    va_list args;
    va_start(args, msg);
    log_append(msg, args);

    log_flush();
}

// panic: terminate application
void laik_panic(const char* msg)
{
    laik_log(LAIK_LL_Panic, "%s", msg);
}


// KV Store

Laik_KVNode* laik_kv_newNode(char* name, Laik_KVNode* parent, Laik_KValue* v)
{
    Laik_KVNode* n = malloc(sizeof(Laik_KVNode));
    if (!n) {
        laik_panic("Out of memory allocating Laik_KVNode object");
        exit(1); // not actually needed, laik_panic never returns
    }

    n->name = name; // take ownership
    n->parent = parent;
    n->value = v;
    n->synched = false;
    n->firstChild = 0;
    n->nextSibling = 0;

    return n;
}

Laik_KVNode* laik_kv_getNode(Laik_KVNode* n, char* path, bool create)
{
    char* sep;
    assert(path != 0);

    while(*path) {
        sep = path;
        while(*sep && (*sep != '/')) sep++;

        Laik_KVNode* cNode = n->firstChild;
        while(cNode) {
            assert(cNode->name != 0);
            if ((strncmp(cNode->name, path, sep - path) == 0) &&
                (cNode->name[sep - path] == 0))
                break;
            cNode = cNode->nextSibling;
        }

        if (cNode == 0) {
            // no match found, create?
            if (!create) return 0;

            cNode = laik_kv_newNode(strndup(path, sep - path), n, 0);
            cNode->nextSibling = n->firstChild;
            n->firstChild = cNode;
        }

        n = cNode;
        if (*sep == 0) break;
    }
    return n;
}

Laik_KValue* laik_kv_setValue(Laik_KVNode* n,
                              char* path, int count, int size, void* value)
{
    Laik_KVNode* nn = laik_kv_getNode(n, path, true);
    assert(nn->value == 0); // should not be set yet

    Laik_KValue* v = malloc(sizeof(Laik_KValue));
    if (!v) {
        laik_panic("Out of memory allocating Laik_KValue object");
        exit(1); // not actually needed, laik_panic never returns
    }

    v->type = LAIK_KV_Struct;
    v->size = size;
    v->vPtr = value;
    v->synched = false;
    v->count = count;

    nn->value = v;
    return v;
}

int laik_kv_getPathLen(Laik_KVNode* n)
{
    int len = 0;
    while(n) {
        len += strlen(n->name);
        n = n->parent;
        if (n) len++;
    }
    return len;
}

char* laik_kv_getPath(Laik_KVNode* n)
{
    static char path[100];

    int len = laik_kv_getPathLen(n);
    assert(len < 100);

    path[len] = 0;
    while(n) {
        int nlen = strlen(n->name);
        len -= nlen;
        assert(len >= 0);
        strncpy(path + len, n->name, nlen);
        n = n->parent;
        if (n) path[--len] = '/';
    }
    assert(len == 0);
    return path;
}

// return the value attached to node reachable by <path> from <n>
Laik_KValue* laik_kv_value(Laik_KVNode* n, char* path)
{
    Laik_KVNode* nn = laik_kv_getNode(n, path, false);

    return nn ? nn->value : 0;
}

// iterate over all children of a node <n>, use 0 for <prev> to get first
Laik_KVNode* laik_kv_next(Laik_KVNode* n, Laik_KVNode* prev)
{
    if (prev) {
        assert(prev->parent == n);
        return prev->nextSibling;
    }
    return n->firstChild;
}

// number of children
int laik_kv_count(Laik_KVNode* n)
{
    Laik_KVNode* cNode = n->firstChild;
    int c = 0;

    while(cNode) {
        c++;
        cNode = cNode->nextSibling;
    }

    return c;
}

// remove child with key, return false if not found
bool laik_kv_remove(Laik_KVNode* n, char* path)
{
    Laik_KVNode* nn = laik_kv_getNode(n, path, false);
    if (!nn) return false;

    // TODO

    return true;
}

// synchronize KV store
void laik_kv_sync(Laik_Instance* inst)
{
    Laik_Backend* b = inst->backend;

    assert(b && b->globalSync);
    (b->globalSync)(inst);
}

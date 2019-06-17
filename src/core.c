/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017, 2018 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
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


#include <laik-internal.h>
#include <laik-backend-mpi.h>
#include <laik-backend-single.h>
#include <laik-backend-tcp.h>

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// default log level
static int laik_loglevel = LAIK_LL_Error;
static FILE* laik_logfile = NULL;
static int laik_logprefix = 2; // 0: none, 1: short, 2:long
static Laik_Instance* laik_loginst = 0;
static int laik_logctr = 0;
// filter
static int laik_log_fromtask = -1;
static int laik_log_totask = -1;

//program name
extern const char *__progname;

//-----------------------------------------------------------------------
// LAIK init/finalize
//
// see corresponding backend code for non-generic initialization of LAIK

// generic LAIK init function
Laik_Instance* laik_init (int* argc, char*** argv)
{
    const char* override = getenv("LAIK_BACKEND");
    Laik_Instance* inst = 0;

#ifdef USE_MPI
    if (inst == 0) {
        // default to MPI if available, or if explicitly wanted
        if ((override == 0) || (strcmp(override, "mpi") == 0)) {
            inst = laik_init_mpi(argc, argv);
        }
    }
#endif

    if (inst == 0) {
        // fall-back to "single" backend as default if MPI is not available, or
        // if "single" backend is explicitly requested
        if ((override == 0) || (strcmp(override, "single") == 0)) {
            (void) argc;
            (void) argv;
            inst = laik_init_single();
        }
    }

#ifdef USE_TCP
    if (inst == 0) {
        if ((override == 0) || (strcmp(override, "tcp") == 0)) {
            inst = laik_init_tcp(argc, argv);
        }
    }
#endif

    if (inst == 0) {
        // Error: unknown backend wanted
        assert(override != 0);

        // create dummy backend for laik_log to work
        laik_init_single();
        laik_log(LAIK_LL_Panic,
                 "Unknwown backend '%s' requested by LAIK_BACKEND", override);
        exit (1);
    }

    // wait for debugger to attach?
    char* rstr = getenv("LAIK_DEBUG_RANK");
    if (rstr) {
        int wrank = atoi(rstr);
        if ((wrank < 0) || (wrank == inst->myid)) {
            // as long as "wait" is 1, wait in loop for debugger
            volatile int wait = 1;
            while(wait) { usleep(10000); }
        }
    }

    return inst;
}


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
        Laik_SwitchStat* ss = laik_newSwitchStat();
        for(int i=0; i<inst->data_count; i++) {
            Laik_Data* d = inst->data[i];
            laik_addSwitchStat(ss, d->stat);

            laik_log_append("  data '%s': ", d->name);
            laik_log_SwitchStat(d->stat);
        }
        if (inst->data_count > 1) {
            laik_log_append("  summary: ");
            laik_log_SwitchStat(ss);
        }
        free(ss);

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
Laik_Instance* laik_new_instance(const Laik_Backend* b,
                                 int size, int myid,
                                 char* location, void* data, void *gdata)
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

    // for logging wall-clock time since LAIK initialization
    gettimeofday(&(instance->init_time), NULL);

    instance->firstSpaceForInstance = 0;

    instance->group_count = 0;
    instance->data_count = 0;
    instance->mapping_count = 0;

    laik_space_init();
    laik_data_init(); // initialize the data module

    instance->control = laik_program_control_init();
    instance->profiling = laik_init_profiling();

    instance->repart_ctrl = 0;

    // logging (TODO: multiple instances)
    laik_loginst = instance;
    char* str = getenv("LAIK_LOG");
    if (str) {
        if (*str == 'n') { laik_logprefix = 0; str++; }
        if (*str == 's') { laik_logprefix = 1; str++; }

        int l = atoi(str);
        if (l > 0)
            laik_loglevel = l;
        else {
            // exit with some help text
            fprintf(stderr, "Unknown LAIK_LOG syntax. Use\n\n"
                    "    LAIK_LOG=[option]level[:rank[-torank]]\n\n"
                    " option : logging option (characters, defaults to none)\n"
                    "            n - no line prefix\n"
                    "            s - use short prefix\n"
                    " level  : minimum logging level (digit, defaults to 0: no logging)\n"
                    " rank   : only log if process has given rank (number, default: no filter)\n"
                    " torank : allow logging for range of ranks [rank;torank] (number)\n");
            exit(1);
        }
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

    if (laik_log_begin(2)) {
        laik_log_append_info();
        laik_log_flush(0);
    }

    // Create a group in this instance with same parameters as the instance.
    // Since it's the first group, this is what laik_world() will return.
    Laik_Group* first_group = laik_create_group (instance);
    first_group->size         = size;
    first_group->myid         = myid;
    first_group->backend_data = gdata;

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

    // space after struct
    g->toParent   = (int*) (((char*)g) + sizeof(Laik_Group));
    g->fromParent = g->toParent + i->size;

    i->group_count++;
    return g;
}

Laik_Instance* laik_inst(Laik_Group* g)
{
    return g->inst;
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

// For a specific group and id (offset into the group), find the offset into the top level group (should be world) equal
// to the referenced rank
int laik_group_location(Laik_Group* group, int id) {
    while(group->parent != NULL) {
        // Ensure we don't go out of bounds
        assert(id >= 0 && id < group->size);
        // Ensure a mapping from this group's ids to the parent group's ids is provided
        assert(group->toParent != NULL);

        id = group->toParent[id];
        group = group->parent;
    }
    assert(id >= 0 && id < group->size);
    return id;
}


//char* laik_group_get_world_location(Laik_Group *group, int id) {
//    assert(id >= 0 && id < group->size);
//
//    int locationOffset = group->toLocation[id];
//
//    Laik_Instance *instance = group->inst;
//    assert(locationOffset >= 0 && locationOffset < instance->size);
//    return instance->location[id];
//}

//TODO: Move to laik_instance
#define LAIK_LOCATION_STORE_MAX_KEY_SIZE 128
#define LAIK_LOCATION_GET_KEY(x,y) snprintf(x, LAIK_LOCATION_STORE_MAX_KEY_SIZE, "location_%i", y)

void laik_synchronize_location_data(Laik_Instance* instance, Laik_Group* synchronizationGroup) {
//    Laik_Space* space = laik_new_space_1d(instance, synchronizationGroup->size * LAIK_LOCATION_MAX_LENGTH);
//    Laik_Data* data = laik_new_data(space, laik_UChar);
//
//    void* base;
//    uint64_t size;
//    laik_map_def1(data, &base, &size);

    if(instance->locationStore == NULL) {
        laik_kvs_new("Laik_Location_Data", instance);
    }

    char *mylocation = laik_mylocation(instance);
    int locationSize = strlen(mylocation);
    char myKey[LAIK_LOCATION_STORE_MAX_KEY_SIZE];
    LAIK_LOCATION_GET_KEY(myKey, laik_group_location(synchronizationGroup, laik_myid(synchronizationGroup)));
    laik_kvs_set(instance->locationStore, myKey, locationSize, mylocation);

    laik_kvs_sync(instance->locationStore);
}

char* laik_get_location_identifier(Laik_Group* group, int id) {
    if(group->inst->locationStore == NULL) {
        return NULL;
    }

    char myKey[LAIK_LOCATION_STORE_MAX_KEY_SIZE];
    LAIK_LOCATION_GET_KEY(myKey, laik_group_location(group, id));

    return laik_kvs_get(group->inst->locationStore, myKey, NULL);
}

// Utilities

char* laik_get_guid(Laik_Instance* i){
    return i->guid;
}

// Logging



// to overwrite environment variable LAIK_LOG
void laik_set_loglevel(int l)
{
    laik_loglevel = l;
}

// check for log level: return true if given log level will be shown
bool laik_log_shown(int l)
{
    return (l >= laik_loglevel);
}

/* Log a message, similar to printf
 *
 * By default, a prefix is added which allows sorting to get stable output
 * from the arbitrarily interleaved output of multiple MPI tasks:
 *
 * == LAIK-<logctr>-T<task> <itermsgctr>.<line> <wtime>
 *
 * logctr : counter incremented at iteration/phase borders
 * task   : task rank in this LAIK instance
 * msgctr : log message counter, reset at each logctr change
 * line   : a line counter if a log message consists of multiple lines
 * wtime  : wall clock time since LAIK instance initialization
 *
 * To build the message step by step:
 * - start: laik_log_begin(<level>)
 * - optionally multiple times: laik_log_append(<msg>, ...)
 * - end with laik_log_flush(<msg>, ...)
 *
 * Or just use log(<level>, <msg>, ...) which internally uses above functions
*/

// buffered logging, not thread-safe

static int current_logLevel = LAIK_LL_None;
static char* current_logBuffer = 0;
static int current_logSize = 0;
static int current_logPos = 0;

bool laik_log_begin(int l)
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
    va_end(args);
}

// increment logging counter used in prefix
void laik_log_inc()
{
    laik_logctr++;
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
    static int last_logctr = 0;
    int line_counter = 0;
    assert(laik_loginst != 0);
    if (last_logctr != laik_logctr) {
        counter = 0;
        last_logctr = laik_logctr;
    }
    counter++;

#define LINE_LEN 100
    // enough for prefix plus one line of log message
    static char buf2[150 + LINE_LEN];
    int off1 = 0, off, off2;

    char* buf1 = current_logBuffer;

    int spaces = 0, last_break = 0;
    bool at_newline = true;

    struct timeval now;
    gettimeofday(&now, NULL);
    double wtime = (double)(now.tv_sec - laik_loginst->init_time.tv_sec) +
                   0.000001 * (now.tv_usec - laik_loginst->init_time.tv_usec);
    int wtime_min = (int) (wtime/60.0);
    double wtime_s = wtime - 60.0 * wtime_min;

    // append prefix at beginning of each line of msg
    while(buf1[off1]) {

        // prefix to allow sorting of log output
        // sorting makes chunks from output of each MPI task
        line_counter++;
        off2 = sprintf(buf2, "%s ", (line_counter == 1) ? "==" : "..");
        if (laik_logprefix == 1)
            off2 += sprintf(buf2+off2, "T%02d | ", laik_loginst->myid);
        else if (laik_logprefix == 2)
            off2 += sprintf(buf2+off2,
                            "LAIK-%04d-T%02d %04d.%02d %2d:%06.3f | ",
                            laik_logctr, laik_loginst->myid,
                            counter, line_counter,
                            wtime_min, wtime_s);
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

        assert(off2 < 150 + LINE_LEN);

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
        va_end(args);
    }

    log_flush();
}

void laik_log(int l, const char* msg, ...)
{
    if (!laik_log_begin(l)) return;

    va_list args;
    va_start(args, msg);
    log_append(msg, args);
    va_end(args);

    log_flush();
}

// panic: terminate application
void laik_panic(const char* msg)
{
    laik_log(LAIK_LL_Panic, "%s", msg);
}


// KV Store



Laik_KVStore* laik_kvs_new(const char* name, Laik_Instance *inst)
{
    Laik_KVStore* kvs = (Laik_KVStore*) malloc(sizeof(Laik_KVStore));

    kvs->name = name;
    kvs->inst = inst;

    kvs->size = 1000;
    kvs->entry = (Laik_KVS_Entry*) malloc(kvs->size * sizeof(Laik_KVS_Entry));
    kvs->used = 0;
    kvs->sorted_upto = 0;

    kvs->myOffSize = 100;      // allow to specify 100 new/changed entries per sync
    kvs->myOffUsed = 0;
    kvs->myOff = (unsigned int*) malloc(kvs->myOffSize * sizeof(int));
    kvs->myOff[0] = 0; // 1st offset into data is always 0
    kvs->myDataSize = 100000; // reserve 100k for data of new/changed entries per sync
    kvs->myDataUsed = 0;
    kvs->myData = (char*) malloc(kvs->myDataSize);
    kvs->in_sync = false;

    return kvs;
}

void laik_kvs_free(Laik_KVStore* kvs)
{
    assert(kvs);

    free(kvs->entry);
    free(kvs->myOff);
    free(kvs->myData);
    free(kvs);
}

// returns true on actual change
// do not take ownership of name/data (do a deep copy)
bool laik_kvs_set(Laik_KVStore* kvs, char* key, unsigned int size, char* data)
{
    assert(data != 0);

    Laik_KVS_Entry* e = laik_kvs_entry(kvs, key);
    if (e && (memcmp(e->data, data, (size_t) size) == 0)) {
        laik_log(1, "set KV entry '%s' (size %d, '%.20s'): already existing",
                 key, size, data);
        return false;
    }

    if (!e) {
        assert(kvs->used < kvs->size);
        e = &kvs->entry[kvs->used];
        kvs->used++;
        e->key = strdup(key);
        e->data = 0;
        e->updated = false;
    }

    laik_log(1, "set %s KV entry '%s' (size %d) to '%.20s'",
             (e->data == 0) ? "new" : "changed", key, size, data);

    if (e->updated && kvs->in_sync) {
        // update from other process and updated ourself differently
        laik_log(LAIK_LL_Panic,
                 "KVS '%s' at key '%s': update inconsistency\n",
                 kvs->name, key);
        exit(1);
    }

    free(e->data);
    e->data = (char*) malloc(size);
    assert(e->data);
    memcpy(e->data, data, size);
    e->size = size;

    if (kvs->in_sync) return true;
    e->updated = true;

    // queue this update for propagation on next sync
    unsigned int nsize = (unsigned int) strlen(key);
    assert(kvs->myOffUsed + 2 < kvs->myOffSize); // 3 new entries
    assert(kvs->myDataUsed + nsize + 1 + size < kvs->myDataSize); // name and data must fit
    assert(kvs->myOff[kvs->myOffUsed] == kvs->myDataUsed); // marker correct?
    memcpy(kvs->myData + kvs->myDataUsed, key, nsize + 1); // include string end '0'
    kvs->myOff[kvs->myOffUsed + 1] = kvs->myDataUsed + nsize + 1;
    kvs->myDataUsed += nsize + 1;
    memcpy(kvs->myData + kvs->myDataUsed, data, size);
    kvs->myOff[kvs->myOffUsed + 2] = kvs->myDataUsed + size;
    kvs->myDataUsed += size;
    kvs->myOffUsed += 2;

    return true;
}

// synchronize KV store
void laik_kvs_sync(Laik_KVStore* kvs)
{
    const Laik_Backend* b = kvs->inst->backend;
    assert(b && b->sync);

    laik_log(1, "sync KVS '%s' (progagating %d/%d entries) ...",
             kvs->name, kvs->myOffUsed / 2, kvs->used);
    kvs->in_sync = true;
    (b->sync)(kvs);
    kvs->in_sync = false;

    // all queued entries sent, remove
    kvs->myOffUsed = 0;
    kvs->myDataUsed = 0;

    for(unsigned int i = 0; i < kvs->used; i++)
        kvs->entry[i].updated = false;

    laik_log(1, "  sync done (now %d entries).", kvs->used);

    laik_kvs_sort(kvs);
}

Laik_KVS_Entry* laik_kvs_entry(Laik_KVStore* kvs, char* key)
{
    Laik_KVS_Entry* e;

    // do binary search in range [0 .. (sorted_upto-1)]
    int low = 0, high = ((int)kvs->sorted_upto) - 1, mid, res;
    //laik_log(1, "  binary search in KVS '%s' for '%s' in [%d,%d]",
    //         kvs->name, key, low, high);
    while(low <= high) {
        mid = (low + high) / 2;
        e = &(kvs->entry[mid]);
        res = strcmp(key, e->key);
        //laik_log(1, "    test [%d] '%s': %d", mid, e->key, res);
        if (res == 0)
            return e;
        if (res < 0)
            high = mid-1;
        else
            low = mid+1;
    }

    // linear search for unsorted items
    for(unsigned int i = kvs->sorted_upto; i < kvs->used; i++) {
        e = &(kvs->entry[i]);
        if (strcmp(e->key, key) == 0)
            return e;
    }

    return 0;
}

char* laik_kvs_data(Laik_KVS_Entry* e, unsigned int *psize)
{
    assert(e);
    if (psize) *psize = e->size;
    return e->data;
}

char* laik_kvs_get(Laik_KVStore* kvs, char* key, unsigned int* psize)
{
    Laik_KVS_Entry* e = laik_kvs_entry(kvs, key);
    if (!e) return 0;

    return laik_kvs_data(e, psize);
}

unsigned int laik_kvs_count(Laik_KVStore* kvs)
{
    assert(kvs);
    return kvs->used;
}

Laik_KVS_Entry* laik_kvs_getn(Laik_KVStore* kvs, unsigned int n)
{
    assert(kvs);
    if (n >= kvs->used) return 0;
    return &(kvs->entry[n]);
}

char* laik_kvs_key(Laik_KVS_Entry* e)
{
    assert(e);
    return e->key;
}

unsigned int laik_kvs_size(Laik_KVS_Entry* e)
{
    assert(e);
    return e->size;
}


unsigned int laik_kvs_copy(Laik_KVS_Entry* e, char* mem, unsigned int size)
{
    assert(e);
    if (e->size < size) size = e->size;
    memcpy(mem, e->data, size);
    return size;
}

static int entrycmp(const void * v1, const void * v2)
{
    const Laik_KVS_Entry* e1 = (const Laik_KVS_Entry*) v1;
    const Laik_KVS_Entry* e2 = (const Laik_KVS_Entry*) v2;
    return strcmp(e1->key, e2->key);
}

void laik_kvs_sort(Laik_KVStore* kvs)
{
    qsort(kvs->entry, kvs->used, sizeof(Laik_KVS_Entry), entrycmp);
    kvs->sorted_upto = kvs->used;
}


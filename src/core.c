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
#include <sys/time.h>

// default log level
static Laik_LogLevel laik_loglevel = LAIK_LL_Error;
static Laik_Instance* laik_loginst = 0;
// filter
static int laik_log_fromtask = -1;
static int laik_log_totask = -1;

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
    assert(inst);
    laik_log(1, "finalizing...");
    if (inst->backend && inst->backend->finalize)
        (*inst->backend->finalize)(inst);

    if (laik_logshown(2)) {
        char s[5000];
        int o;

        o = sprintf(s, "switch statistics (this task):\n");
        for(int i=0; i<inst->data_count; i++) {
            Laik_Data* d = inst->data[i];
            o += sprintf(s+o, "  data '%s': ", d->name);
            o += laik_getSwitchStat(s+o, d->stat);
        }
        laik_log(2, s);
    }

    free(inst->control);
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
    instance = (Laik_Instance*) malloc(sizeof(Laik_Instance));

    instance->backend = b;
    instance->backend_data = data;
    instance->size = size;
    instance->myid = myid;
    instance->mylocation = strdup(location);

    instance->firstSpaceForInstance = 0;

    instance->group_count = 0;
    instance->data_count = 0;
    instance->mapping_count = 0;

    laik_space_init();
    laik_data_init();
    instance->control = laik_program_control_init();

    instance->do_profiling = false;

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

    g = (Laik_Group*) malloc(sizeof(Laik_Group) + 2 * (i->size) * sizeof(int));
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

int getIntListStr(char* s, int len, int* list)
{
    int o;
    o = sprintf(s, "[");
    for(int i = 0; i < len; i++) {
        o += sprintf(s+o, "%s%d",
                     (i>0) ? ", ":"", list[i]);
    }
    o += sprintf(s+o, "]");
    return o;
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

    if (laik_logshown(1)) {
        char s[500];
        int o;
        o = sprintf(s, "%d (size %d, myid %d) => %d (size %d, myid %d):",
                    g->gid, g->size, g->myid, g2->gid, g2->size, g2->myid);
        o += sprintf(s+o, "\n  fromParent (to shrinked)  : ");
        o += getIntListStr(s+o, g->size, g2->fromParent);
        o += sprintf(s+o, "\n  toParent   (from shrinked): ");
        o += getIntListStr(s+o, g2->size, g2->toParent);

        laik_log(1, "shrink group: %s\n", s);
    }

    return g2;
}



// Profiling

static Laik_Instance* laik_profinst = 0;

void laik_enable_profiling(Laik_Instance* i)
{
    if (laik_profinst) {
        if (laik_profinst == i) return;
        laik_profinst->do_profiling = false;
    }
    laik_profinst = i;
    if (!i) return;

    i->do_profiling = true;
    i->time_backend = 0.0;
    i->time_total = 0.0;
}

void laik_reset_profiling(Laik_Instance* i){
    if(laik_profinst){
        if(laik_profinst == i) {
            if(i->do_profiling){
            i->do_profiling = true;
            i->time_backend = 0.0;
            i->time_total = 0.0;
        }
    }
    }
}

double laik_get_total_time()
{
    if (!laik_profinst) return 0.0;

    return laik_profinst->time_total;
}

double laik_get_backend_time()
{
    if (!laik_profinst) return 0.0;

    return laik_profinst->time_backend;
}



// Logging

double laik_wtime()
{
  struct timeval tv;
  gettimeofday(&tv, 0);

  return tv.tv_sec+1e-6*tv.tv_usec;
}

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
*/
void laik_log(Laik_LogLevel l, const char* msg, ...)
{
    if (l < laik_loglevel) return;
    if (laik_log_fromtask >= 0) {
        assert(laik_log_totask >= laik_log_fromtask);
        if (laik_loginst->myid < laik_log_fromtask) return;
        if (laik_loginst->myid > laik_log_totask) return;
    }

    assert(laik_loginst != 0);
    const char* lstr = 0;
    switch(l) {
        case LAIK_LL_Warning: lstr = "Warning"; break;
        case LAIK_LL_Error:   lstr = "ERROR"; break;
        case LAIK_LL_Panic:   lstr = "PANIC"; break;
        default: break;
    }

    static char buf1[2000];
    va_list args;
    va_start(args, msg);
    vsprintf(buf1, msg, args);

    // counters for stable output
    static int counter = 0;
    static int last_phase_counter = 0;
    int line_counter = 0;
    if (last_phase_counter != laik_loginst->control->phase_counter) {
        counter = 0;
        last_phase_counter = laik_loginst->control->phase_counter;
    }
    counter++;

    // print at once to not mix output from multiple (MPI) tasks
    static char buf2[3000];
    int off1 = 0, off2 = 0, off;

    const char* phase = laik_loginst->control->cur_phase_name;
    if (!phase) phase = "";
    int spaces = 0, last_break = 0;
    bool at_newline = true;

    // append prefix at beginning of each line of msg
    while(buf1[off1]) {

        // prefix
        line_counter++;
        off2 += sprintf(buf2+off2,
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
                if (spaces + (off - off1) > 70) {
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
    }
    fprintf(stderr, "%s", buf2);

    // terminate program on panic
    if (l == LAIK_LL_Panic) exit(1);
}

// panic: terminate application
void laik_panic(const char* msg)
{
    laik_log(LAIK_LL_Panic, msg);
}

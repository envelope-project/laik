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

// for string.h to declare strdup
#define __STDC_WANT_LIB_EXT2__ 1

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <laik-backend-ulfm.h>


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

#ifdef USE_ULFM
    if (inst == NULL) {
        // default to ULFM if available, or if explicitly wanted (superset of MPI)
        if ((override == 0) || (strcmp(override, "ulfm") == 0)) {
            inst = laik_init_ulfm(argc, argv);
        }
    }
#endif

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
        if ((wrank < 0) || (wrank == inst->mylocationid)) {
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
            // Bugfix by VB: Check whether d was previously freed
            if(d) {
                laik_addSwitchStat(ss, d->stat);

                laik_log_append("  data '%s': ", d->name);
                laik_log_SwitchStat(d->stat);
            }
        }
        if (inst->data_count > 1) {
            laik_log_append("  summary: ");
            laik_log_SwitchStat(ss);
        }
        free(ss);

        laik_log_flush(0);
    }

    laik_close_profiling_file(inst);
    laik_free_profiling(inst);
    free(inst->control);

    laik_log_cleanup(inst);
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
    instance->locations = size;    // initial number of locations
    instance->mylocationid = myid; // initially, myid is my locationid
    instance->mylocation = strdup(location);
    instance->locationStore = 0;
    instance->location = 0; // set at location sync

    instance->spaceStore = 0;

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
    instance->errorHandler = NULL;

    // logging (TODO: multiple instances)
    laik_log_init(instance);

    if (laik_log_begin(2)) {
        laik_log_append_info();
        laik_log_flush(0);
    }

    // Create 'world' group with same parameters as the instance.
    Laik_Group* world = laik_create_group(instance, size);
    world->size = size;
    world->myid = myid;
    world->backend_data = gdata;
    instance->world = world;
    // initial location IDs are the same as process IDs in initial world
    for(int i = 0; i < size; i++)
        world->locationid[i] = i;

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
Laik_Group* laik_create_group(Laik_Instance* i, int maxsize)
{
    assert(i->group_count < MAX_GROUPS);

    Laik_Group* g;

    // with 3 arrays, 2 with number of processes as size, other with parent size
    g = malloc(sizeof(Laik_Group) + (3 * maxsize) * sizeof(int));
    if (!g) {
        laik_panic("Out of memory allocating Laik_Group object");
        exit(1); // not actually needed, laik_panic never returns
    }
    i->group[i->group_count] = g;

    g->inst = i;
    g->gid = i->group_count;
    g->size = 0; // yet invalid;
    g->maxsize = maxsize;
    g->backend_data = 0;
    g->parent = 0;

    // space after struct
    g->toParent   = (int*) (((char*)g) + sizeof(Laik_Group));
    g->fromParent = g->toParent + maxsize;
    g->locationid = g->fromParent + maxsize;

    i->group_count++;
    return g;
}

Laik_Instance* laik_inst(Laik_Group* g)
{
    return g->inst;
}

Laik_Group* laik_world(Laik_Instance* i)
{
    return i->world;
}

void laik_set_world(Laik_Instance* i, Laik_Group* world)
{
    // TODO: check that removed processes do not appear in any
    //       active group of this instance

    assert(world->inst == i);
    i->world = world;
}

// create a clone of <g>, derived from <g>.
Laik_Group* laik_clone_group(Laik_Group* g)
{
    Laik_Group* g2 = laik_create_group(g->inst, g->size);
    g2->parent = g;
    g2->size = g->size;
    g2->myid = g->myid;

    for(int i=0; i < g->size; i++) {
        g2->toParent[i] = i;
        g2->fromParent[i] = i;
        g2->locationid[i] = g->locationid[i];
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
        g2->locationid[o] = g->locationid[i];
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
        laik_log_append("\n  toLocation (in shrinked): ");
        laik_log_IntList(g2->size, g2->locationid);
        laik_log_flush(0);
    }

    return g2;
}

int laik_group_locationid(Laik_Group *group, int id)
{
    assert(id >= 0 && id < group->size);
    return group->locationid[id];
}

static char* locationkey(int loc)
{
    static char key[10];
    snprintf(key, 10, "%i", loc);
    return key;
}

static void update_location(Laik_KVStore* s, Laik_KVS_Entry* e)
{
    int lid = atoi(e->key);
    assert((lid >= 0) && (lid < s->inst->locations));
    s->inst->location[lid] = e->value;
    laik_log(1, "location for locID %d (key '%s') updated to '%s'",
             lid, e->key, e->value);
}

static void remove_location(Laik_KVStore* s, char* key)
{
    int lid = atoi(key);
    assert((lid >= 0) && (lid < s->inst->locations));
    laik_log(1, "location for locID %d (key '%s') removed (was '%s')",
             lid, key, s->inst->location[lid]);
    s->inst->location[lid] = 0;
}


// synchronize location strings via KVS among processes in current world
// TODO: only sync new locations after growth of instance
void laik_sync_location(Laik_Instance *instance)
{
    if (instance->locationStore == NULL) {
        instance->locationStore = laik_kvs_new("location", instance);
        instance->location = (char**) malloc((unsigned)instance->locations * sizeof(char*));
        assert(instance->location != 0);
        for(int i = 0; i < instance->locations; i++)
            instance->location[i] = 0;

        // register function to update direct access to location
        laik_kvs_reg_callbacks(instance->locationStore,
                               update_location, update_location, remove_location);
    }

    Laik_Group* world = laik_world(instance);
    char* mylocation = laik_mylocation(instance);
    int mylocationid = laik_group_locationid(world, laik_myid(world));
    // update location array directly with own location
    instance->location[mylocationid] = mylocation;

    char* myKey = locationkey(mylocationid);
    laik_kvs_sets(instance->locationStore, myKey, mylocation);
    laik_kvs_sync(instance->locationStore);
}

// get location string identifier from process index in given group
char* laik_group_location(Laik_Group *group, int id)
{
    if (group->inst->location == NULL)
        return NULL;

    int lid = laik_group_locationid(group, id);
    assert(lid >= 0 && lid < group->inst->locations);
    return group->inst->location[lid];
}


// Utilities

char* laik_get_guid(Laik_Instance* i){
    return i->guid;
}


Laik_Backend_Error_Handler* laik_error_handler_get(Laik_Instance* instance) {
    if(instance == NULL) {
        return NULL;
    }
    return instance->errorHandler;
}

void laik_error_handler_set(Laik_Instance* instance, Laik_Backend_Error_Handler* errorHandler) {
    if(instance == NULL) {
        return;
    }
    instance->errorHandler = errorHandler;
}

#include <sys/time.h>

double getTime(Laik_Instance* inst) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (double)(now.tv_sec - inst->init_time.tv_sec) +
           0.000001 * (now.tv_usec - inst->init_time.tv_usec);
}

double getWallTime() {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (double)(now.tv_sec) + 0.000001 * (now.tv_usec);
}

double getVSize() {
    long rss = 0L;
    FILE* fp = NULL;
    if ( (fp = fopen( "/proc/self/statm", "r" )) == NULL )
        return (size_t)0L;      /* Can't open? */
    if ( fscanf( fp, "%*s%ld", &rss ) != 1 )
    {
        fclose( fp );
        return (size_t)0L;      /* Can't read? */
    }
    fclose( fp );
    return ((size_t)rss * (size_t)sysconf( _SC_PAGESIZE)) / 1024.0;
}

double getNSize(Laik_Instance *inst) {
    size_t data = 0;
    for(int i=0; i<inst->data_count; i++) {
        Laik_Data* d = inst->data[i];
        // This data may have been freed, so check that it still exists
        if(d) {
            data += d->stat->byteRecvCount;
            data += d->stat->byteSendCount;
            data += d->stat->byteReduceCount;
        }
    }
    return data / 1024.0;
}

double start;

void setStartTime(double startTime) {
    start = startTime;
}
double getStartTime() {
    return start;
}

int eventNum = 0;
int getEventNum() {
    return eventNum++;
}

bool TRACE_ENABLED = false;

bool isTraceEnabled() {
    return TRACE_ENABLED;
}

void setTraceEnabled(bool newTraceSetting) {
    TRACE_ENABLED = newTraceSetting;
}

void TRACE_INIT(int myRank) {
    char *envVar = getenv("LAIK_APPLICATION_TRACE_ENABLED");
    if(envVar != NULL && strcmp(envVar, "1") == 0) {
        setTraceEnabled(true);
    }
    if(isTraceEnabled() && myRank == 0) {
        printf("===,EVENT_SEQ,EVENT_TYPE,RANK,TIME,DURATION,WALLTIME,ITER,MEM,NET,EXTRA\n");
    }
}

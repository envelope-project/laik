/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"
#include "laik-backend-mpi.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef USE_MPI

Laik_Instance* laik_init_mpi(int* argc, char*** argv)
{
    printf("ERROR: LAIK is not compiled with MPI backend.");
    exit(1);
}

void laik_mpi_finalize() {}

#else // USE_MPI

#include <mpi.h>


// forward decls, types/structs , global variables

void laik_mpi_finalize();
void laik_mpi_execTransition(Laik_Data* d, Laik_Transition *t,
                             Laik_MappingList* fromList,
                             Laik_MappingList* toList);
// update backend specific data for group if needed
void laik_mpi_updateGroup(Laik_Group*);

static Laik_Backend laik_backend_mpi = {"MPI Backend",
                                        laik_mpi_finalize, 0, 0,
                                        laik_mpi_execTransition,
                                        laik_mpi_updateGroup};
static Laik_Instance* mpi_instance = 0;

typedef struct _MPIData MPIData;
struct _MPIData {
    MPI_Comm comm;
    bool didInit;
};

typedef struct _MPIGroupData MPIGroupData;
struct _MPIGroupData {
    MPI_Comm comm;
};

// intentially make MPI backend buggy by setting LAIK_MPI_BUG=1
// useful to ensure that a test is sentitive to backend bugs
static int mpi_bug = 0;

// buffer space for messages if packing/unpacking from/to not-1d layout
// is necessary
// TODO: if we go to asynchronous messages, this needs to be dynamic per data
#define PACKBUFSIZE (10*1024*1024)
//#define PACKBUFSIZE (10*800)
static char packbuf[PACKBUFSIZE];

Laik_Instance* laik_init_mpi(int* argc, char*** argv)
{
    if (mpi_instance) return mpi_instance;

    MPIData* d = malloc(sizeof(MPIData));
    if (!d) {
        laik_panic("Out of memory allocating MPIData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    d->didInit = false;
    d->comm = MPI_COMM_WORLD;

    if (argc) {
        MPI_Init(argc, argv);
        d->didInit = true;
    }

    int size, rank;
    MPI_Comm_size(d->comm, &size);
    MPI_Comm_rank(d->comm, &rank);

    // Get the name of the processor
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int name_len;
    MPI_Get_processor_name(processor_name, &name_len);

    Laik_Instance* inst;
    inst = laik_new_instance(&laik_backend_mpi, size, rank,
                             processor_name, d);

    sprintf(inst->guid, "%d", rank);

    // group world

    MPIGroupData* gd = malloc(sizeof(MPIGroupData));
    if (!gd) {
        laik_panic("Out of memory allocating MPIGroupData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    gd->comm = MPI_COMM_WORLD;

    Laik_Group* g = laik_create_group(inst);
    g->inst = inst;
    g->gid = 0;
    g->size = inst->size;
    g->myid = inst->myid;
    g->backend_data = gd;

    laik_log(1, "MPI backend initialized (location '%s', pid %d)\n",
             inst->mylocation, (int) getpid());

    // for intentionally buggy MPI backend behavior
    char* str = getenv("LAIK_MPI_BUG");
    if (str) mpi_bug = atoi(str);

    // wait for debugger to attach?
    char* rstr = getenv("LAIK_DEBUG_RANK");
    if (rstr) {
        int wrank = atoi(rstr);
        if ((wrank < 0) || (wrank == rank)) {
            // as long as "wait" is 1, wait in loop for debugger
            volatile int wait = 1;
            while(wait) { usleep(10000); }
        }
    }

    mpi_instance = inst;
    return inst;
}

MPIData* mpiData(Laik_Instance* i)
{
    return (MPIData*) i->backend_data;
}

MPIGroupData* mpiGroupData(Laik_Group* g)
{
    return (MPIGroupData*) g->backend_data;
}

void laik_mpi_finalize()
{
    if (mpiData(mpi_instance)->didInit)
        MPI_Finalize();
}

// calculate MPI communicator for group <g>
void laik_mpi_updateGroup(Laik_Group* g)
{
    // TODO: only supports shrinking of parent for now
    assert(g->parent);
    assert(g->parent->size > g->size);


    laik_log(1, "MPI backend updateGroup: parent %d (size %d, myid %d) "
             "=> group %d (size %d, myid %d)",
             g->parent->gid, g->parent->size, g->parent->myid,
             g->gid, g->size, g->myid);

    // only interesting if this task is still part of parent
    if (g->parent->myid < 0) return;

    MPIGroupData* gdParent = (MPIGroupData*) g->parent->backend_data;
    assert(gdParent);

    MPIGroupData* gd = (MPIGroupData*) g->backend_data;
    assert(gd == 0); // must not be updated yet
    gd = malloc(sizeof(MPIGroupData));
    if (!gd) {
        laik_panic("Out of memory allocating MPIGroupData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    g->backend_data = gd;

    laik_log(1, "MPI Comm_split: old myid %d => new myid %d",
             g->parent->myid, g->fromParent[g->parent->myid]);

    MPI_Comm_split(gdParent->comm,
                   (g->fromParent[g->parent->myid] < 0) ? MPI_UNDEFINED : 0,
                   g->parent->myid, &(gd->comm));
}

static
MPI_Datatype getMPIDataType(Laik_Data* d)
{
    MPI_Datatype mpiDataType;
    if      (d->type == laik_Double) mpiDataType = MPI_DOUBLE;
    else if (d->type == laik_Float) mpiDataType = MPI_FLOAT;
    else assert(0);

    return mpiDataType;
}


void laik_mpi_execTransition(Laik_Data* d, Laik_Transition* t,
                             Laik_MappingList* fromList, Laik_MappingList* toList)
{
    int myid  = d->group->myid;
    int dims = d->space->dims;
    Laik_SwitchStat* ss = d->stat;

    laik_log(1, "MPI backend execute transition:\n"
             "  data '%s', group %d (size %d, myid %d)\n"
             "  actions: %d reductions, %d sends, %d recvs",
             d->name, d->group->gid, d->group->size, myid,
             t->redCount, t->sendCount, t->recvCount);

    if (myid < 0) {
        // this task is not part of the communicator to use
        return;
    }

    MPIGroupData* gd = mpiGroupData(d->group);
    assert(gd); // must have been updated by laik_mpi_updateGroup()
    MPI_Comm comm = gd->comm;

    if (t->redCount > 0) {
        assert(dims == 1);
        bool reuseMap = false; // set to true if we reused fromMap for toMap
        // we only support reductions within one mapping
        assert((fromList != 0) && (fromList->count == 1));
        Laik_Mapping* fromMap = &(fromList->map[0]);
        assert(fromMap);
        // result goes to all tasks or just one => toList may be 0
        if (toList)
            assert(toList->count == 1);
        Laik_Mapping* toMap = toList ? &(toList->map[0]) : 0;

        if (toMap && (toMap->base == 0)) {
            // we may reuse previous mapping
            // only possible if is has the same size
            if (toMap->count == fromMap->count) {
                // we can do IN_PLACE reduction
                toMap->base = fromMap->base;
                reuseMap = true;
            }
            else {
                laik_allocateMap(toMap, ss);
                assert(toMap->base != 0);
            }
        }

        for(int i=0; i < t->redCount; i++) {
            struct redTOp* op = &(t->red[i]);
            uint64_t from = op->slc.from.i[0];
            uint64_t to   = op->slc.to.i[0];

            char* fromBase = fromMap ? fromMap->base : 0;
            char* toBase = toMap ? toMap->base : 0;

            assert(fromBase != 0);
            // if current task is receiver, toBase should be allocated
            if (laik_isInGroup(t, op->outputGroup, myid))
                assert(toBase != 0);
            else
                toBase = 0; // no interest in receiving anything

            MPI_Op mpiRedOp;
            switch(op->redOp) {
            case LAIK_RO_Sum: mpiRedOp = MPI_SUM; break;
            default: assert(0);
            }

            MPI_Datatype mpiDataType = getMPIDataType(d);

            int rootTask;
            if (op->outputGroup == -1) rootTask = -1;
            else {
                // TODO: support more then 1 receiver
                assert(t->group[op->outputGroup].count == 1);
                rootTask = t->group[op->outputGroup].task[0];
            }

            if (laik_log_begin(1)) {
                laik_log_append("MPI Reduce (root ");
                if (rootTask == -1)
                    laik_log_append("ALL");
                else
                    laik_log_append("%d", rootTask);
                if (fromBase == toBase)
                    laik_log_append(", IN_PLACE");
                laik_log_flush("): (%lu - %lu) sliceNo in/out %d/%d, "
                               "elemsize %d, baseptr from/to %p/%p\n",
                               from, to,
                               op->myInputSliceNo, op->myOutputSliceNo,
                               d->elemsize, fromBase, toBase);
            }

            if (ss) {
                ss->reduceCount++;
                ss->reducedBytes += (to - from) * d->elemsize;
            }

            assert(from >= fromMap->requiredSlice.from.i[0]);
            fromBase += (from - fromMap->requiredSlice.from.i[0]) * d->elemsize;
            if (toBase) {
                assert(from >= toMap->requiredSlice.from.i[0]);
                toBase += (from - toMap->requiredSlice.from.i[0]) * d->elemsize;
            }

            if (rootTask == -1) {
                if (fromBase == toBase)
                    MPI_Allreduce(MPI_IN_PLACE, toBase, to - from,
                                  mpiDataType, mpiRedOp, comm);
                else
                    MPI_Allreduce(fromBase, toBase, to - from,
                                  mpiDataType, mpiRedOp, comm);
            }
            else {
                if (fromBase == toBase)
                    MPI_Reduce(MPI_IN_PLACE, toBase, to - from,
                               mpiDataType, mpiRedOp, rootTask, comm);
                else
                    MPI_Reduce(fromBase, toBase, to - from,
                               mpiDataType, mpiRedOp, rootTask, comm);
            }
        }
        if (reuseMap) {
            assert(fromMap);
            // do not free memory behind fromMap, as we reused the mapping
            fromMap->base = 0;
        }
    }

    // use 2x <task count> phases to avoid deadlocks
    // - count phases X: 0..<count-1>
    //     - receive from <task X> if <task X> lower rank
    //     - send to <task X> if <task X> is higher rank
    // - count phases Y: 0..<count-1>
    //     - receive from <task count-Y> if it is higher rank
    //     - send to <task count-1-Y> if it is lower rank
    //
    // TODO: prepare communication schedule with sorted transitions actions!

    int count = d->group->size;
    for(int phase = 0; phase < 2*count; phase++) {
        int task = (phase < count) ? phase : (2*count-phase-1);
        bool sendToHigher   = (phase < count);
        bool recvFromLower  = (phase < count);
        bool sendToLower    = (phase >= count);
        bool recvFromHigher = (phase >= count);

        // receive
        for(int i=0; i < t->recvCount; i++) {
            struct recvTOp* op = &(t->recv[i]);
            if (task != op->fromTask) continue;
            if (recvFromLower  && (myid < task)) continue;
            if (recvFromHigher && (myid > task)) continue;

            if (laik_log_begin(1)) {
                laik_log_append("MPI Recv ");
                laik_log_Slice(dims, &(op->slc));
                laik_log_flush(" from T%d", op->fromTask);
            }

            assert(myid != op->fromTask);

            assert(op->mapNo < toList->count);
            Laik_Mapping* toMap = &(toList->map[op->mapNo]);
            assert(toMap != 0);
            if (toMap->base == 0) {
                // space not yet allocated
                laik_allocateMap(toMap, ss);
                assert(toMap->base != 0);
            }

            MPI_Status s;
            uint64_t count;

            MPI_Datatype mpiDataType = getMPIDataType(d);

            // TODO:
            // - tag 1 may conflict with application
            // - check status

            if (dims == 1) {
                // we directly support 1d data layouts

                // from global to receiver-local indexes
                uint64_t from = op->slc.from.i[0] - toMap->requiredSlice.from.i[0];
                uint64_t to   = op->slc.to.i[0] - toMap->requiredSlice.from.i[0];
                count = to - from;

                laik_log(1, "  direct recv to local [%lu;%lu[, slice/map %d, "
                         "elemsize %d, baseptr %p\n",
                         from, to, op->sliceNo, op->mapNo,
                         d->elemsize, toMap->base);

                if (mpi_bug > 0) {
                    // intentional bug: ignore small amounts of data received
                    if (count < 1000) {
                        char dummy[8000];
                        MPI_Recv(dummy, count,
                                 mpiDataType, op->fromTask, 1, comm, &s);
                        continue;
                    }
                }

                MPI_Recv(toMap->base + from * d->elemsize, count,
                         mpiDataType, op->fromTask, 1, comm, &s);
            }
            else {
                // use temporary receive buffer and layout-specific unpack

                // the used layout must support unpacking
                assert(toMap->layout->unpack);

                Laik_Index idx = op->slc.from;
                count = 0;
                int recvCount, unpacked;
                while(1) {
                    MPI_Recv(packbuf, PACKBUFSIZE / d->elemsize,
                             mpiDataType, op->fromTask, 1, comm, &s);
                    MPI_Get_count(&s, mpiDataType, &recvCount);
                    unpacked = (toMap->layout->unpack)(toMap, &(op->slc), &idx,
                                                       packbuf,
                                                       recvCount * d->elemsize);
                    assert(recvCount == unpacked);
                    count += unpacked;
                    if (laik_index_isEqual(dims, &idx, &(op->slc.to))) break;
                }
                assert(count == laik_slice_size(dims, &(op->slc)));
            }


            if (ss) {
                ss->recvCount++;
                ss->receivedBytes += count * d->elemsize;
            }


        }

        // send
        for(int i=0; i < t->sendCount; i++) {
            struct sendTOp* op = &(t->send[i]);
            if (task != op->toTask) continue;
            if (sendToLower  && (myid < task)) continue;
            if (sendToHigher && (myid > task)) continue;

            if (laik_log_begin(1)) {
                laik_log_append("MPI Send ");
                laik_log_Slice(dims, &(op->slc));
                laik_log_flush(" to T%d", op->toTask);
            }

            assert(myid != op->toTask);

            assert(op->mapNo < fromList->count);
            Laik_Mapping* fromMap = &(fromList->map[op->mapNo]);
            // data to send must exist in local memory
            assert(fromMap);
            if (!fromMap->base) {
                laik_log_begin(LAIK_LL_Panic);
                laik_log_append("About to send data ('%s', slice ", d->name);
                laik_log_Slice(dims, &(op->slc));
                laik_log_flush(") to preserve it for the next phase as"
                                " requested by you, but it never was written"
                                " to in the previous phase. Fix your code!");
                assert(0);
            }

            uint64_t count;
            MPI_Datatype mpiDataType = getMPIDataType(d);

            if (dims == 1) {
                // we directly support 1d data layouts

                // from global to sender-local indexes
                uint64_t from = op->slc.from.i[0] - fromMap->requiredSlice.from.i[0];
                uint64_t to   = op->slc.to.i[0] - fromMap->requiredSlice.from.i[0];
                count = to - from;

                laik_log(1, "  direct send: from local [%lu;%lu[, slice/map %d/%d, "
                            "elemsize %d, baseptr %p\n",
                         from, to, op->sliceNo, op->mapNo,
                         d->elemsize, fromMap->base);

                // TODO: tag 1 may conflict with application
                MPI_Send(fromMap->base + from * d->elemsize, count,
                         mpiDataType, op->toTask, 1, comm);
            }
            else {
                // use temporary receive buffer and layout-specific unpack

                // the used layout must support packing
                assert(fromMap->layout->pack);

                Laik_Index idx = op->slc.from;
                uint64_t size = laik_slice_size(dims, &(op->slc));
                assert(size > 0);
                int packed;
                count = 0;
                while(1) {
                    packed = (fromMap->layout->pack)(fromMap, &(op->slc), &idx,
                                                     packbuf, PACKBUFSIZE);
                    assert(packed > 0);
                    MPI_Send(packbuf, packed,
                             mpiDataType, op->toTask, 1, comm);
                    count += packed;
                    if (laik_index_isEqual(dims, &idx, &(op->slc.to))) break;
                }
                assert(count == size);
            }

            if (ss) {
                ss->sendCount++;
                ss->sentBytes += count * d->elemsize;
            }
        }
    
    }
}

#endif // USE_MPI

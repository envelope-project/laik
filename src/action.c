/*
 * This file is part of the LAIK library.
 * Copyright (c) 2018 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// create a new action sequence object, usable for the given LAIK instance
Laik_ActionSeq* laik_aseq_new(Laik_Instance *inst)
{
    Laik_ActionSeq* as = malloc(sizeof(Laik_ActionSeq));
    as->inst = inst;
    as->backend = 0;

    for(int i = 0; i < ASEQ_CONTEXTS_MAX; i++)
        as->context[i] = 0;
    as->contextCount = 0;

    for(int i = 0; i < ASEQ_BUFFER_MAX; i++) {
        as->buf[i] = 0;
        as->bufSize[i] = 0;
    }
    as->bufferCount = 0;
    as->bufReserveCount = 0;

    for(int i = 0; i < ASEQ_COPYENTRY_MAX; i++)
        as->ce[i] = 0;
    as->ceCount = 0;

    as->actionCount = 0;
    as->action = 0;
    as->roundCount = 0;

    as->newAction = 0;
    as->newActionCount = 0;
    as->newBytesUsed = 0;
    as->newBytesAlloc = 0;
    as->newRoundCount = 0;

    as->sendCount = 0;
    as->recvCount = 0;
    as->reduceCount = 0;

    return as;
}

// free all resources allocated for an action sequence
// this may include backend-specific resources
void laik_aseq_free(Laik_ActionSeq* as)
{
    if (as->backend) {
        // ask backend to do its own cleanup for this action sequence
        (as->backend->cleanup)(as);
    }

    Laik_TransitionContext* tc = as->context[0];

    for(int i = 0; i < as->bufferCount; i++) {
        if (as->bufSize[i] == 0) continue;

        laik_log(1, "    free buffer %d: %d bytes\n", i, as->bufSize[i]);
        free(as->buf[i]);

        // update allocation statistics
        tc->data->stat->freeCount++;
        tc->data->stat->freedBytes += as->bufSize[i];
    }

    for(int i = 0; i < as->contextCount; i++)
        free(as->context[i]);

    for(int i = 0; i < as->ceCount; i++)
        free(as->ce[i]);

    free(as->action);
    free(as->newAction);

    free(as);
}

// get sum of sizes of all allocated temporary buffers
int laik_aseq_bufsize(Laik_ActionSeq* as)
{
    int total = 0;
    for(int i = 0; i < as->bufferCount; i++)
        total += as->bufSize[i];

    return total;
}

// append an invalid action of given size
Laik_Action* laik_aseq_addAction(Laik_ActionSeq* as, int size, int round)
{
    // TODO: this only works for an array of BackendAction's for now
    assert(size == sizeof(Laik_BackendAction));
    assert(round < 255);

    if (as->newBytesUsed + size > as->newBytesAlloc) {
        // enlarge buffer by more than <size>
        as->newBytesAlloc = (as->newBytesAlloc + size + 50) * 2;
        as->newAction = realloc(as->newAction, as->newBytesAlloc);
        if (!as->newAction) {
            laik_panic("Out of memory allocating memory for Laik_ActionSeq");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    Laik_Action* a = (Laik_Action*) ( ((char*)as->newAction) + as->newBytesUsed);
    as->newBytesUsed += size;
    as->newActionCount++;

    a->type = LAIK_AT_Invalid;
    a->len = size;
    a->round = round;
    a->tid = 0; // always refer to single transition context

    if (as->newRoundCount <= round)
        as->newRoundCount = round + 1;

    return a;
}

// discard any new built actions (e.g. if there was no change to old seq)
void laik_aseq_discardNewActions(Laik_ActionSeq* as)
{
    // keep temporary space around for further transformations
    as->newBytesUsed = 0;
    as->newActionCount = 0;
    as->newRoundCount = 0;
}

// finish building an action sequence, activate the new built sequence
void laik_aseq_activateNewActions(Laik_ActionSeq* as)
{
    // free old sequence
    free(as->action);

    // copy into new space with size as needed
    as->action = malloc(as->newBytesUsed);
    memcpy(as->action, as->newAction, as->newBytesUsed);
    as->actionCount = as->newActionCount;
    as->roundCount = as->newRoundCount;

    laik_aseq_discardNewActions(as);
}

// free temporary space used for building a new sequence
void laik_aseq_freeTempSpace(Laik_ActionSeq* as)
{
    // only call this if we are not currently building a new sequence
    assert(as->newActionCount == 0);

    free(as->newAction);
    as->newAction = 0;
    as->newBytesAlloc = 0;
}


Laik_BackendAction* laik_aseq_addBAction(Laik_ActionSeq* as, int round)
{
    Laik_BackendAction* ba;
    ba = (Laik_BackendAction*) laik_aseq_addAction(as,
                                                   sizeof(Laik_BackendAction),
                                                   round);
    return ba;
}

void laik_aseq_initTContext(Laik_TransitionContext* tc,
                            Laik_Data* data, Laik_Transition* transition,
                            Laik_MappingList* fromList,
                            Laik_MappingList* toList)
{
    tc->data = data;
    tc->transition = transition;
    tc->fromList = fromList;
    tc->toList = toList;
}

int laik_aseq_addTContext(Laik_ActionSeq* as,
                          Laik_Data* data, Laik_Transition* transition,
                          Laik_MappingList* fromList,
                          Laik_MappingList* toList)
{
    Laik_TransitionContext* tc = malloc(sizeof(Laik_TransitionContext));
    laik_aseq_initTContext(tc, data, transition, fromList, toList);

    assert(as->contextCount < ASEQ_CONTEXTS_MAX);
    int contextID = as->contextCount;
    as->contextCount++;

    assert(as->context[contextID] == 0);
    as->context[contextID] = tc;

    return contextID;
}

// append action to do the transition specified by the transition context ID
// call laik_aseq_addTContext() before to add a context and get the ID.
void laik_aseq_addTExec(Laik_ActionSeq* as, int tid)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, 0);

    a->type = LAIK_AT_TExec;
    a->tid = tid;
}

// append action to reserve buffer space
// if <bufID> is negative, a new ID is generated (always > 100)
// returns bufID.
//
// bufID < 100 are reserved for buffers already allocated (buf[bufID]).
// in a final pass, all buffer reservations must be collected, the buffer
// allocated (with ID 0), and the references to this buffer replaced
// by references into buffer 0. These actions can be removed afterwards.
int laik_aseq_addBufReserve(Laik_ActionSeq* as, int size, int bufID)
{
    if (bufID < 0) {
        // generate new buf ID
        // only return IDs > 0. ID 0 is reserved for actually allocated buffer
        bufID = as->bufReserveCount + 100;
        as->bufReserveCount++;
    }
    else if (bufID > as->bufReserveCount + 99)
        as->bufReserveCount = bufID - 99;

    // BufReserve in round 0: allocation is done before exec
    Laik_BackendAction* a = laik_aseq_addBAction(as, 0);
    a->type = LAIK_AT_BufReserve;
    a->count = size;
    a->bufID = bufID;
    a->offset = 0;

    return bufID;
}

// append send action to buffer referencing a previous reserve action
void laik_aseq_addRBufSend(Laik_ActionSeq* as, int round,
                           int bufID, int byteOffset,
                           int count, int to)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_RBufSend;
    a->bufID = bufID;
    a->offset = byteOffset;
    a->count = count;
    a->peer_rank = to;
}

// append recv action into buffer referencing a previous reserve action
void laik_aseq_addRBufRecv(Laik_ActionSeq* as, int round,
                           int bufID, int byteOffset,
                           int count, int from)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_RBufRecv;
    a->bufID = bufID;
    a->offset = byteOffset;
    a->count = count;
    a->peer_rank = from;
}

// append action to call a local reduce operation
// using buffer referenced by a previous reserve action and toBuf as input
void laik_aseq_addRBufLocalReduce(Laik_ActionSeq* as, int round,
                                  Laik_Type* dtype,
                                  Laik_ReductionOperation redOp,
                                  int fromBufID, int fromByteOffset,
                                  char* toBuf, int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_RBufLocalReduce;
    a->dtype = dtype;
    a->redOp = redOp;
    a->toBuf = toBuf;
    a->count = count;
    a->bufID = fromBufID;
    a->offset = fromByteOffset;
}

// append action to call an init operation
void laik_aseq_addBufInit(Laik_ActionSeq* as, int round,
                          Laik_Type* dtype,
                          Laik_ReductionOperation redOp,
                          char* toBuf, int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_BufInit;
    a->dtype = dtype;
    a->redOp = redOp;
    a->toBuf = toBuf;
    a->count = count;
}

// append action to call a copy operation from/to a buffer
// if fromBuf is 0, use a buffer referenced by a previous reserve action
void laik_aseq_addRBufCopy(Laik_ActionSeq* as, int round,
                           int fromBufID, int fromByteOffset,
                           char* toBuf, int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_RBufCopy;
    a->bufID = fromBufID;
    a->offset = fromByteOffset;
    a->toBuf = toBuf;
    a->count = count;
}

// append action to call a copy operation from/to a buffer
void laik_aseq_addBufCopy(Laik_ActionSeq* as, int round,
                          char* fromBuf, char* toBuf, int count)
{
    assert(fromBuf != toBuf);

    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_BufCopy;
    a->round = round;
    a->fromBuf = fromBuf;
    a->toBuf = toBuf;
    a->count = count;
}


// append send action from a mapping with offset
void laik_aseq_addMapSend(Laik_ActionSeq* as, int round,
                          int fromMapNo, uint64_t off,
                          int count, int to)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_MapSend;
    a->fromMapNo = fromMapNo;
    a->offset = off;
    a->count = count;
    a->peer_rank = to;

    as->sendCount += count;
}

// append send action from a buffer
void laik_aseq_addBufSend(Laik_ActionSeq* as, int round,
                          char* fromBuf, int count, int to)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_BufSend;
    a->fromBuf = fromBuf;
    a->count = count;
    a->peer_rank = to;

    as->sendCount += count;
}

// append recv action into a mapping with offset
void laik_aseq_addMapRecv(Laik_ActionSeq* as, int round,
                          int toMapNo, uint64_t off,
                          int count, int from)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_MapRecv;
    a->toMapNo = toMapNo;
    a->offset = off;
    a->count = count;
    a->peer_rank = from;

    as->recvCount += count;
}

// append recv action into a buffer
void laik_aseq_addBufRecv(Laik_ActionSeq* as, int round,
                          char* toBuf, int count, int from)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_BufRecv;
    a->toBuf = toBuf;
    a->count = count;
    a->peer_rank = from;

    as->recvCount += count;
}


void laik_aseq_initPackAndSend(Laik_BackendAction* a,
                               Laik_Mapping* fromMap, int dims, Laik_Slice* slc,
                               int to)
{
    a->type = LAIK_AT_PackAndSend;
    a->map = fromMap;
    a->dims = dims;
    a->slc = slc;
    a->peer_rank = to;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}

void laik_aseq_addPackAndSend(Laik_ActionSeq* as, int round,
                              Laik_Mapping* fromMap, Laik_Slice* slc, int to)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;
    laik_aseq_initPackAndSend(a, fromMap, dims, slc, to);

    as->sendCount += a->count;
}

void laik_aseq_addPackToRBuf(Laik_ActionSeq* as, int round,
                             Laik_Mapping* fromMap, Laik_Slice* slc,
                             int toBufID, int toByteOffset)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;

    a->type = LAIK_AT_PackToRBuf;
    a->map = fromMap;
    a->dims = dims;
    a->slc = slc;
    a->bufID = toBufID;
    a->offset = toByteOffset;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}

void laik_aseq_addPackToBuf(Laik_ActionSeq* as, int round,
                            Laik_Mapping* fromMap, Laik_Slice* slc, char* toBuf)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;

    a->type = LAIK_AT_PackToBuf;
    a->map = fromMap;
    a->dims = dims;
    a->slc = slc;
    a->toBuf = toBuf;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}


void laik_actions_initMapPackAndSend(Laik_BackendAction* a,
                                     int fromMapNo, int dims, Laik_Slice* slc,
                                     int to)
{
    a->type = LAIK_AT_MapPackAndSend;
    a->fromMapNo = fromMapNo;
    a->dims = dims;
    a->slc = slc;
    a->peer_rank = to;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}

void laik_aseq_addMapPackAndSend(Laik_ActionSeq* as, int round,
                                 int fromMapNo, Laik_Slice* slc, int to)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;
    laik_actions_initMapPackAndSend(a, fromMapNo, dims, slc, to);

    as->sendCount += a->count;
}

void laik_aseq_addMapPackToRBuf(Laik_ActionSeq* as, int round,
                                int fromMapNo, Laik_Slice* slc,
                                int toBufID, int toByteOffset)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;

    a->type = LAIK_AT_MapPackToRBuf;
    a->fromMapNo = fromMapNo;
    a->dims = dims;
    a->slc = slc;
    a->bufID = toBufID;
    a->offset = toByteOffset;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}


void laik_aseq_initRecvAndUnpack(Laik_BackendAction* a,
                                 Laik_Mapping* toMap, int dims, Laik_Slice* slc,
                                 int from)
{
    a->type = LAIK_AT_RecvAndUnpack;
    a->map = toMap;
    a->dims = dims;
    a->slc = slc;
    a->peer_rank = from;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}

void laik_aseq_addRecvAndUnpack(Laik_ActionSeq* as, int round,
                                Laik_Mapping* toMap, Laik_Slice* slc, int from)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;
    laik_aseq_initRecvAndUnpack(a, toMap, dims, slc, from);

    as->recvCount += a->count;
}

void laik_aseq_addUnpackFromRBuf(Laik_ActionSeq* as, int round,
                                 int fromBufID, int fromByteOffset,
                                 Laik_Mapping* toMap, Laik_Slice* slc)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;

    a->type = LAIK_AT_UnpackFromRBuf;
    a->dims = dims;
    a->bufID = fromBufID;
    a->offset = fromByteOffset;
    a->map = toMap;
    a->slc = slc;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}

void laik_aseq_addUnpackFromBuf(Laik_ActionSeq* as, int round,
                                char* fromBuf, Laik_Mapping* toMap, Laik_Slice* slc)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;

    a->type = LAIK_AT_UnpackFromBuf;
    a->dims = dims;
    a->fromBuf = fromBuf;
    a->map = toMap;
    a->slc = slc;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}

void laik_actions_initMapRecvAndUnpack(Laik_BackendAction* a,
                                       int toMapNo, int dims, Laik_Slice* slc,
                                       int from)
{
    a->type = LAIK_AT_MapRecvAndUnpack;
    a->toMapNo = toMapNo;
    a->dims = dims;
    a->slc = slc;
    a->peer_rank = from;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}

void laik_actions_addMapRecvAndUnpack(Laik_ActionSeq* as, int round,
                                      int toMapNo, Laik_Slice* slc, int from)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;
    laik_actions_initMapRecvAndUnpack(a, toMapNo, dims, slc, from);
}

void laik_aseq_addMapUnpackFromRBuf(Laik_ActionSeq* as, int round,
                                    int fromBufID, int fromByteOffset,
                                    int toMapNo, Laik_Slice* slc)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;

    a->type = LAIK_AT_MapUnpackFromRBuf;
    a->dims = dims;
    a->bufID = fromBufID;
    a->offset = fromByteOffset;
    a->toMapNo = toMapNo;
    a->slc = slc;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}

void laik_aseq_initReduce(Laik_BackendAction* a,
                          char* fromBuf, char* toBuf, int count,
                          int rootTask, Laik_ReductionOperation redOp)
{
    a->type = LAIK_AT_Reduce;
    a->fromBuf = fromBuf;
    a->toBuf = toBuf;
    a->count = count;
    a->peer_rank = rootTask;
    a->redOp = redOp;
}


void laik_aseq_addReduce(Laik_ActionSeq* as, int round,
                         char* fromBuf, char* toBuf, int count,
                         int rootTask, Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    laik_aseq_initReduce(a, fromBuf, toBuf, count, rootTask, redOp);

    assert(count > 0);
    as->reduceCount += count;
}

void laik_actions_addRBufReduce(Laik_ActionSeq* as, int round,
                                int bufID, int byteOffset, int count,
                                int rootTask, Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    a->type = LAIK_AT_RBufReduce;
    a->bufID = bufID;
    a->offset = byteOffset;
    a->count = count;
    a->peer_rank = rootTask;
    a->redOp = redOp;

    assert(count > 0);
    as->reduceCount += count;
}

// append group reduce action
void laik_aseq_addMapGroupReduce(Laik_ActionSeq* as, int round,
                                 int inputGroup, int outputGroup,
                                 int myInputMapNo, int myOutputMapNo,
                                 Laik_Slice* slc, Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;

    a->type = LAIK_AT_MapGroupReduce;
    a->inputGroup = inputGroup;
    a->outputGroup = outputGroup;
    a->fromMapNo = myInputMapNo;
    a->toMapNo = myOutputMapNo;
    a->dims = dims;
    a->slc = slc;
    a->count = laik_slice_size(dims, slc);
    a->redOp = redOp;

    assert(a->count > 0);
    as->reduceCount += a->count;
}


void laik_aseq_initGroupReduce(Laik_BackendAction* a,
                               int inputGroup, int outputGroup,
                               char* fromBuf, char* toBuf, int count,
                               Laik_ReductionOperation redOp)
{
    a->type = LAIK_AT_GroupReduce;
    a->inputGroup = inputGroup;
    a->outputGroup = outputGroup;
    a->fromBuf = fromBuf;
    a->toBuf = toBuf;
    a->count = count;
    a->redOp = redOp;
}

void laik_aseq_addGroupReduce(Laik_ActionSeq* as, int round,
                              int inputGroup, int outputGroup,
                              char* fromBuf, char* toBuf, int count,
                              Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    laik_aseq_initGroupReduce(a, inputGroup, outputGroup,
                              fromBuf, toBuf, count, redOp);

    assert(count > 0);
    as->reduceCount += count;
}

// similar to addGroupReduce
void laik_aseq_addRBufGroupReduce(Laik_ActionSeq* as, int round,
                                  int inputGroup, int outputGroup,
                                  int bufID, int byteOffset, int count,
                                  Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_RBufGroupReduce;
    a->inputGroup = inputGroup;
    a->outputGroup = outputGroup;
    a->bufID = bufID;
    a->offset = byteOffset;
    a->count = count;
    a->redOp = redOp;

    assert(count > 0);
    as->reduceCount += count;
}



void laik_aseq_addCopyToBuf(Laik_ActionSeq* as, int round,
                            Laik_CopyEntry* ce, char* toBuf, int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_CopyToBuf;
    a->ce = ce;
    a->toBuf = toBuf;
    a->count = count;
}

void laik_aseq_addCopyFromBuf(Laik_ActionSeq* as, int round,
                              Laik_CopyEntry* ce, char* fromBuf, int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_CopyFromBuf;
    a->ce = ce;
    a->fromBuf = fromBuf;
    a->count = count;
}

void laik_aseq_addCopyToRBuf(Laik_ActionSeq* as, int round,
                             Laik_CopyEntry* ce,
                             int toBufID, int toByteOffset, int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_CopyToRBuf;
    a->ce = ce;
    a->bufID = toBufID;
    a->offset = toByteOffset;
    a->count = count;

}

void laik_aseq_addCopyFromRBuf(Laik_ActionSeq* as, int round,
                               Laik_CopyEntry* ce,
                               int fromBufID, int fromByteOffset, int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->type = LAIK_AT_CopyFromRBuf;
    a->ce = ce;
    a->bufID = fromBufID;
    a->offset = fromByteOffset;
    a->count = count;
}

bool laik_action_isSend(Laik_BackendAction* ba)
{
    switch(ba->type) {
    case LAIK_AT_MapSend:
    case LAIK_AT_BufSend:
    case LAIK_AT_RBufSend:
    case LAIK_AT_MapPackAndSend:
    case LAIK_AT_PackAndSend:
        return true;
    }
    return false;
}

bool laik_action_isRecv(Laik_BackendAction* ba)
{
    switch(ba->type) {
    case LAIK_AT_MapRecv:
    case LAIK_AT_BufRecv:
    case LAIK_AT_RBufRecv:
    case LAIK_AT_MapRecvAndUnpack:
    case LAIK_AT_RecvAndUnpack:
        return true;
    }
    return false;
}


// add all reduce ops from a transition to an ActionSeq.
void laik_aseq_addReds(Laik_ActionSeq* as, int round,
                       Laik_Data* data, Laik_Transition* t)
{
    Laik_TransitionContext* tc = as->context[0];
    assert(tc->data == data);
    assert(tc->transition == t);
    assert(t->group->myid >= 0);

    for(int i=0; i < t->redCount; i++) {
        struct redTOp* op = &(t->red[i]);
        laik_aseq_addMapGroupReduce(as, round,
                                    op->inputGroup, op->outputGroup,
                                    op->myInputMapNo, op->myOutputMapNo,
                                    &(op->slc), op->redOp);
    }
}

// add all receive ops from a transition to an ActionSeq.
// before execution, a deadlock-avoidance action sorting is required
void laik_aseq_addRecvs(Laik_ActionSeq* as, int round,
                        Laik_Data* data, Laik_Transition* t)
{
    Laik_TransitionContext* tc = as->context[0];
    assert(tc->data == data);
    assert(tc->transition == t);
    assert(t->group->myid >= 0);

    for(int i=0; i < t->recvCount; i++) {
        struct recvTOp* op = &(t->recv[i]);
        laik_actions_addMapRecvAndUnpack(as, round, op->mapNo,
                                         &(op->slc), op->fromTask);
    }
}

// add all send ops from a transition to an ActionSeq.
// before execution, a deadlock-avoidance action sorting is required
void laik_aseq_addSends(Laik_ActionSeq* as, int round,
                        Laik_Data* data, Laik_Transition* t)
{
    Laik_TransitionContext* tc = as->context[0];
    assert(tc->data == data);
    assert(tc->transition == t);
    assert(t->group->myid >= 0);

    for(int i=0; i < t->sendCount; i++) {
        struct sendTOp* op = &(t->send[i]);
        laik_aseq_addMapPackAndSend(as, round, op->mapNo,
                                    &(op->slc), op->toTask);
    }
}



// collect buffer reservation actions and update actions referencing them
// works in-place; BufReserve actions are marked as NOP but not removed
// can be called ASEQ_BUFFER_MAX times, allocating a new buffer on each call
// TODO: convert from in-place
bool laik_aseq_allocBuffer(Laik_ActionSeq* as)
{
    int rCount = 0, rActions = 0;
    assert(as->bufferCount < ASEQ_BUFFER_MAX);
    assert(as->buf[as->bufferCount] == 0); // nothing allocated yet

    Laik_TransitionContext* tc = as->context[0];
    int elemsize = tc->data->elemsize;

    Laik_BackendAction** resAction;
    resAction = malloc(as->bufReserveCount * sizeof(Laik_BackendAction*));
    for(int i = 0; i < as->bufReserveCount; i++)
        resAction[i] = 0; // reservation not seen yet for ID (i-100)

    uint64_t bufSize = 0;
    for(int i = 0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);
        switch(ba->type) {
        case LAIK_AT_BufReserve:
            // reservation already processed and allocated
            if (ba->bufID < ASEQ_BUFFER_MAX) break;

            ba->offset = bufSize;
            assert(ba->bufID >= 100);
            assert(ba->bufID < 100 + as->bufReserveCount);
            resAction[ba->bufID - 100] = ba;
            ba->bufID = as->bufferCount; // mark as processed
            bufSize += ba->count;
            rCount++;
            break;

        case LAIK_AT_RBufCopy:
        case LAIK_AT_RBufLocalReduce:
        case LAIK_AT_RBufSend:
        case LAIK_AT_RBufRecv:
        case LAIK_AT_RBufReduce:
        case LAIK_AT_PackToRBuf:
        case LAIK_AT_UnpackFromRBuf:
        case LAIK_AT_CopyFromRBuf:
        case LAIK_AT_CopyToRBuf:
        case LAIK_AT_RBufGroupReduce: {
            // action with allocated reservation
            if (ba->bufID < ASEQ_BUFFER_MAX) break;

            assert(ba->bufID >= 100);
            assert(ba->bufID < 100 + as->bufReserveCount);
            Laik_BackendAction* ra = resAction[ba->bufID - 100];
            assert(ra != 0);
            assert(ba->count > 0);
            assert(ba->offset + (uint64_t)(ba->count * elemsize) <= (uint64_t) ra->count);

            ba->offset += ra->offset;
            ba->bufID = as->bufferCount; // reference into allocated buffer
            rActions++;
            break;
        }

        default:
            break;
        }
    }

    if (bufSize == 0) {
        free(resAction);
        return false;
    }


    char* buf = malloc(bufSize);
    assert(buf != 0);

    // update allocation statistics
    tc->data->stat->mallocCount++;
    tc->data->stat->mallocedBytes += bufSize;

    // eventually modify actions, now that buffer allocation is known
    for(int i = 0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);
        switch(ba->type) {
        case LAIK_AT_BufReserve:
            ba->type = LAIK_AT_Nop;
            break;
        case LAIK_AT_RBufSend:
            ba->fromBuf = buf + ba->offset;
            ba->type = LAIK_AT_BufSend;
            break;
        case LAIK_AT_RBufRecv:
            ba->toBuf = buf + ba->offset;
            ba->type = LAIK_AT_BufRecv;
            break;
        case LAIK_AT_PackToRBuf:
            ba->toBuf = buf + ba->offset;
            ba->type = LAIK_AT_PackToBuf;
            break;
        case LAIK_AT_UnpackFromRBuf:
            ba->fromBuf = buf + ba->offset;
            ba->type = LAIK_AT_UnpackFromBuf;
            break;
        case LAIK_AT_CopyFromRBuf:
            ba->fromBuf = buf + ba->offset;
            ba->type = LAIK_AT_CopyFromBuf;
            break;
        case LAIK_AT_CopyToRBuf:
            ba->toBuf = buf + ba->offset;
            ba->type = LAIK_AT_CopyToBuf;
            break;
        case LAIK_AT_RBufReduce:
            ba->fromBuf = buf + ba->offset;
            ba->toBuf   = buf + ba->offset;
            ba->type = LAIK_AT_Reduce;
            break;
        case LAIK_AT_RBufGroupReduce:
            ba->fromBuf = buf + ba->offset;
            ba->toBuf   = buf + ba->offset;
            ba->type = LAIK_AT_GroupReduce;
            break;
        default: break;
        }
    }
    as->bufSize[as->bufferCount] = bufSize;
    as->buf[as->bufferCount] = buf;

    if (laik_log_begin(1)) {
        laik_log_append("RBuf alloc %d: %d reservations, %d RBuf actions, %llu bytes at %p",
                        as->bufferCount, rCount, rActions, (long long unsigned) bufSize,
                        (void*) as->buf[as->bufferCount]);
        for(int i = 0; i < as->bufReserveCount; i++) {
            if (resAction[i] == 0) continue;
            laik_log_append("\n    RBuf %d (len %d) ==> off %llu at %p",
                            i + 100, resAction[i]->count,
                            (long long unsigned) resAction[i]->offset,
                            (void*) (buf + resAction[i]->offset));
        }
        laik_log_flush(0);
    }

    assert(rCount == as->bufReserveCount);
    free(resAction);

    // start again with bufID 100 for next reservations
    as->bufReserveCount = 0;
    as->bufferCount++;

    return true;
}


// append actions to <as>
// allows to change round of added action when <round> >= 0
void laik_aseq_add(Laik_BackendAction* ba, Laik_ActionSeq* as, int round)
{
    if (round < 0)
        round = ba->round;

    switch(ba->type) {
    case LAIK_AT_Nop:
        // no need to copy a NOP operation
        break;

    case LAIK_AT_BufReserve:
        // can ignored if already processed
        if (ba->bufID < ASEQ_BUFFER_MAX) break;

        laik_aseq_addBufReserve(as, ba->count, ba->bufID);
        break;

    case LAIK_AT_MapSend:
        laik_aseq_addMapSend(as, round,
                             ba->fromMapNo, ba->offset,
                             ba->count, ba->peer_rank);
        break;

    case LAIK_AT_BufSend:
        laik_aseq_addBufSend(as, round,
                             ba->fromBuf, ba->count, ba->peer_rank);
        break;

    case LAIK_AT_RBufSend:
        laik_aseq_addRBufSend(as, round,
                              ba->bufID, ba->offset,
                              ba->count, ba->peer_rank);
        break;

    case LAIK_AT_MapRecv:
        laik_aseq_addMapRecv(as, round,
                             ba->toMapNo, ba->offset,
                             ba->count, ba->peer_rank);
        break;

    case LAIK_AT_BufRecv:
        laik_aseq_addBufRecv(as, round,
                             ba->toBuf, ba->count, ba->peer_rank);
        break;

    case LAIK_AT_RBufRecv:
        laik_aseq_addRBufRecv(as, round,
                              ba->bufID, ba->offset,
                              ba->count, ba->peer_rank);
        break;

    case LAIK_AT_MapPackToRBuf:
        laik_aseq_addMapPackToRBuf(as, round,
                                   ba->fromMapNo, ba->slc, ba->bufID, ba->offset);
        break;

    case LAIK_AT_PackToRBuf:
        laik_aseq_addPackToRBuf(as, round,
                                ba->map, ba->slc, ba->bufID, ba->offset);
        break;

    case LAIK_AT_PackToBuf:
        laik_aseq_addPackToBuf(as, round,
                               ba->map, ba->slc, ba->toBuf);
        break;

    case LAIK_AT_MapPackAndSend:
        laik_aseq_addMapPackAndSend(as, round,
                                    ba->fromMapNo, ba->slc, ba->peer_rank);
        break;

    case LAIK_AT_PackAndSend:
        laik_aseq_addPackAndSend(as, round,
                                 ba->map, ba->slc, ba->peer_rank);
        break;

    case LAIK_AT_MapUnpackFromRBuf:
        laik_aseq_addMapUnpackFromRBuf(as, round,
                                       ba->bufID, ba->offset,
                                       ba->toMapNo, ba->slc);
        break;

    case LAIK_AT_UnpackFromRBuf:
        laik_aseq_addUnpackFromRBuf(as, round,
                                    ba->bufID, ba->offset,
                                    ba->map, ba->slc);
        break;

    case LAIK_AT_UnpackFromBuf:
        laik_aseq_addUnpackFromBuf(as, round,
                                   ba->fromBuf, ba->map, ba->slc);
        break;

    case LAIK_AT_MapRecvAndUnpack:
        laik_actions_addMapRecvAndUnpack(as, round,
                                         ba->toMapNo, ba->slc, ba->peer_rank);
        break;

    case LAIK_AT_RecvAndUnpack:
        laik_aseq_addRecvAndUnpack(as, round,
                                   ba->map, ba->slc, ba->peer_rank);
        break;

    case LAIK_AT_BufCopy:
        laik_aseq_addBufCopy(as, round,
                             ba->fromBuf, ba->toBuf, ba->count);
        break;

    case LAIK_AT_RBufCopy:
        laik_aseq_addRBufCopy(as, round,
                              ba->bufID, ba->offset,
                              ba->toBuf, ba->count);
        break;


    case LAIK_AT_CopyFromBuf:
        laik_aseq_addCopyFromBuf(as, round,
                                 ba->ce, ba->fromBuf, ba->count);
        break;

    case LAIK_AT_CopyToBuf:
        laik_aseq_addCopyToBuf(as, round,
                               ba->ce, ba->toBuf, ba->count);
        break;

    case LAIK_AT_CopyFromRBuf:
        laik_aseq_addCopyFromRBuf(as, round,
                                  ba->ce, ba->bufID, ba->offset, ba->count);
        break;

    case LAIK_AT_CopyToRBuf:
        laik_aseq_addCopyToRBuf(as, round,
                                ba->ce, ba->bufID, ba->offset, ba->count);
        break;

    case LAIK_AT_RBufReduce:
        laik_actions_addRBufReduce(as, round,
                                   ba->bufID, ba->offset, ba->count,
                                   ba->peer_rank, ba->redOp);
        break;

    case LAIK_AT_Reduce:
        laik_aseq_addReduce(as, round,
                            ba->fromBuf, ba->toBuf, ba->count,
                            ba->peer_rank, ba->redOp);
        break;

    case LAIK_AT_MapGroupReduce:
        laik_aseq_addMapGroupReduce(as, round,
                                    ba->inputGroup, ba->outputGroup,
                                    ba->fromMapNo, ba->toMapNo,
                                    ba->slc, ba->redOp);
        break;

    case LAIK_AT_GroupReduce:
        laik_aseq_addGroupReduce(as, round,
                                 ba->inputGroup, ba->outputGroup,
                                 ba->fromBuf, ba->toBuf,
                                 ba->count, ba->redOp);
        break;

    case LAIK_AT_RBufGroupReduce:
        laik_aseq_addRBufGroupReduce(as, round,
                                     ba->inputGroup, ba->outputGroup,
                                     ba->bufID, ba->offset,
                                     ba->count, ba->redOp);
        break;

    case LAIK_AT_RBufLocalReduce:
        laik_aseq_addRBufLocalReduce(as, round,
                                     ba->dtype, ba->redOp,
                                     ba->bufID, ba->offset,
                                     ba->toBuf, ba->count);
        break;

    case LAIK_AT_BufInit:
        laik_aseq_addBufInit(as, round,
                             ba->dtype, ba->redOp,
                             ba->toBuf, ba->count);
        break;

    default:
        laik_log(LAIK_LL_Panic,
                 "laik_actions_add: unknown action %d", ba->type);
        assert(0);
    }
}


// just copy actions from oldAS into as
void laik_aseq_copySeq(Laik_ActionSeq* as)
{
    assert(as->newActionCount == 0);
    for(int i = 0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);
        laik_aseq_add(ba, as, -1);
    }
    laik_aseq_activateNewActions(as);
}


// helpers for action combining

static bool isSameBufSend(Laik_BackendAction* a, int round, int rank)
{
    if (a->type != LAIK_AT_BufSend) return false;
    if (a->round != round) return false;
    if (a->peer_rank != rank) return false;
    return true;
}

static bool isSameBufRecv(Laik_BackendAction* a, int round, int rank)
{
    if (a->type != LAIK_AT_BufRecv) return false;
    if (a->round != round) return false;
    if (a->peer_rank != rank) return false;
    return true;
}

static bool isSameGroupReduce(Laik_BackendAction* a,
                       int round, int inputGroup, int outputGroup,
                       Laik_ReductionOperation redOp)
{
    if (a->type != LAIK_AT_GroupReduce) return false;
    if (a->round != round) return false;
    if (a->inputGroup != inputGroup) return false;
    if (a->outputGroup != outputGroup) return false;
    if (a->redOp != redOp) return false;
    return true;
}

static bool isSameReduce(Laik_BackendAction* a, int round, int root,
                         Laik_ReductionOperation redOp)
{
    if (a->type != LAIK_AT_Reduce) return false;
    if (a->round != round) return false;
    if (a->peer_rank != root) return false;
    if (a->redOp != redOp) return false;
    return true;
}


/* Merge send/recv/groupReduce/reduce actions from "oldAS" into "as".
 *
 * We merge actions with same communication partners, marking actions
 * already merged.
 * A merging of similar actions results in up to 3 new actions:
 * - a multi-copy actions with copy ranges, to copy into a temporary buffer
 *   (only if this process provides input)
 * - the send/recv/groupReduce/reduce action on the larger temporary buffer
 * - a multi-copy actions with copy ranges, to copy from a temporary buffer
 *   (only if this process receives output)
 * Before generating the new actions, merge candidates are identified
 * and space required for temporary buffers and copy ranges.
 *
 * Action round numbers are spreaded by *3+1, allowing space for added
 * combine/split copy actions before/after.
 *
 * This merge transformation is easy to see in LAIK_LOG=1 output of the
 * "the markov2 -f ..." test.
 *
 * TODO: Instead of doing merging in one big step for all action types,
 * we could do it for each seperately, and also generate individual buffer
 * reservation actions.
 *
 * TODO: Instead of nested loops to find actions to combine, do sorting
 * before: (1) by round, (2) by action type, ...
 */
bool laik_aseq_combineActions(Laik_ActionSeq* as)
{
    int combineGroupReduce = 1;
    int combineReduce = 1;

    // must not have new actions, we want to start a new build
    assert(as->newActionCount == 0);

    Laik_TransitionContext* tc = as->context[0];
    Laik_Data* d = tc->data;
    int elemsize = d->elemsize;
    // used for combining GroupReduce actions
    int myid = tc->transition->group->myid;

    // unmark all actions first
    // all actions will be marked on combining, to not process them twice
    for(int i = 0; i < as->actionCount; i++)
        as->action[i].mark = 0;

    // first pass: how much buffer space / copy range elements is needed?
    int bufSize = 0, copyRanges = 0;
    for(int i = 0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);

        // skip already combined actions
        if (ba->mark == 1) continue;

        switch(ba->type) {
        case LAIK_AT_BufSend: {
            // combine all BufSend actions in same round with same target rank
            int round = ba->round;
            int rank = ba->peer_rank;
            int count = ba->count;
            int actionCount = 1;
            for(int j = i+1; j < as->actionCount; j++) {
                Laik_BackendAction* ba2 = &(as->action[j]);
                if (!isSameBufSend(ba2, round, rank)) continue;

                ba2->mark = 1;
                count += ba2->count;
                actionCount++;
            }
            if (actionCount > 1) {
                bufSize += count;
                copyRanges += actionCount;
            }
            break;
        }

        case LAIK_AT_BufRecv: {
            // combine all BufRecv actions in same round with same source rank
            int round = ba->round;
            int rank = ba->peer_rank;
            int count = ba->count;
            int actionCount = 1;
            for(int j = i+1; j < as->actionCount; j++) {
                Laik_BackendAction* ba2 = &(as->action[j]);
                if (!isSameBufRecv(ba2, round, rank)) continue;

                ba2->mark = 1;
                count += ba2->count;
                actionCount++;
            }
            if (actionCount > 1) {
                bufSize += count;
                copyRanges += actionCount;
            }
            break;
        }

        case LAIK_AT_GroupReduce: {
            if (!combineGroupReduce) break;

            // combine all GroupReduce actions with same
            // inputGroup, outputGroup, and redOp
            int round = ba->round;
            int iGroup = ba->inputGroup;
            int oGroup = ba->outputGroup;
            Laik_ReductionOperation redOp = ba->redOp;
            int count = ba->count;
            int actionCount = 1;
            for(int j = i+1; j < as->actionCount; j++) {
                Laik_BackendAction* ba2 = &(as->action[j]);
                if (!isSameGroupReduce(ba2, round, iGroup, oGroup, redOp))
                    continue;

                ba2->mark = 1;
                count += ba2->count;
                actionCount++;
            }
            if (actionCount > 1) {
                bufSize += count;
                if (laik_trans_isInGroup(tc->transition, iGroup, myid))
                    copyRanges += actionCount;
                if (laik_trans_isInGroup(tc->transition, oGroup, myid))
                    copyRanges += actionCount;
            }
            break;
        }

        case LAIK_AT_Reduce: {
            if (!combineReduce) break;

            // combine all reduce actions with same root and redOp
            int round = ba->round;
            int root = ba->peer_rank;
            Laik_ReductionOperation redOp = ba->redOp;
            int count = ba->count;
            int actionCount = 1;
            for(int j = i+1; j < as->actionCount; j++) {
                Laik_BackendAction* ba2 = &(as->action[j]);
                if (!isSameReduce(ba2, round, root, redOp)) continue;

                ba2->mark = 1;
                count += ba2->count;
                actionCount++;
            }
            if (actionCount > 1) {
                bufSize += count;
                // always providing input, copy input ranges
                copyRanges += actionCount;
                // if I want result, we can reuse the input ranges
                if ((root == myid) || (root == -1))
                    copyRanges += actionCount;
            }
            break;
        }

        default:
            // nothing to merge for other actions
            break;
        }
    }

    if (bufSize == 0) {
        assert(copyRanges == 0);
        assert(as->newActionCount == 0);
        return false;
    }

    assert(copyRanges > 0);
    Laik_CopyEntry* ce = malloc(copyRanges * sizeof(Laik_CopyEntry));

    assert(as->ceCount < ASEQ_COPYENTRY_MAX);
    assert(as->ce[as->ceCount] == 0);
    as->ce[as->ceCount] = ce;
    as->ceCount++;

    int bufID = laik_aseq_addBufReserve(as, bufSize * elemsize, -1);

    laik_log(1, "Reservation for combined actions: length %d x %d, ranges %d",
             bufSize, elemsize, copyRanges);

    // unmark all actions: restart for finding same type of actions
    for(int i = 0; i < as->actionCount; i++)
        as->action[i].mark = 0;

    // second pass: add merged actions
    int bufOff = 0;
    int rangeOff = 0;

    for(int i = 0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);

        // skip already processed actions
        if (ba->mark == 1) continue;

        switch(ba->type) {
        case LAIK_AT_BufSend: {
            int round = ba->round;
            int rank = ba->peer_rank;
            int count = ba->count;
            int actionCount = 1;
            for(int j = i+1; j < as->actionCount; j++) {
                Laik_BackendAction* ba2 = &(as->action[j]);
                if (!isSameBufSend(ba2, round, rank)) continue;

                ba2->mark = 1;
                count += ba2->count;
                actionCount++;
            }
            if (actionCount > 1) {
                //laik_log(1,"Send Seq %d - %d, rangeOff %d, bufOff %d, count %d",
                //         i, j, rangeOff, bufOff, count);
                laik_aseq_addCopyToRBuf(as, 3 * ba->round,
                                        ce + rangeOff,
                                        bufID, 0,
                                        actionCount);
                laik_aseq_addRBufSend(as, 3 * ba->round + 1,
                                      bufID, bufOff * elemsize,
                                      count, rank);
                int oldRangeOff = rangeOff;
                for(int k = i; k < as->actionCount; k++) {
                    Laik_BackendAction* ba2 = &(as->action[k]);
                    if (!isSameBufSend(ba2, round, rank)) continue;

                    assert(rangeOff < copyRanges);
                    ce[rangeOff].ptr = ba2->fromBuf;
                    ce[rangeOff].bytes = ba2->count * elemsize;
                    ce[rangeOff].offset = bufOff * elemsize;
                    bufOff += ba2->count;
                    rangeOff++;
                }
                assert(oldRangeOff + actionCount == rangeOff);
            }
            else
                laik_aseq_addBufSend(as, 3 * ba->round + 1,
                                     ba->fromBuf, count, rank);
            break;
        }

        case LAIK_AT_BufRecv: {
            int round = ba->round;
            int rank = ba->peer_rank;
            int count = ba->count;
            int actionCount = 1;
            for(int j = i+1; j < as->actionCount; j++) {
                Laik_BackendAction* ba2 = &(as->action[j]);
                if (!isSameBufRecv(ba2, round, rank)) continue;

                ba2->mark = 1;
                count += ba2->count;
                actionCount++;
            }
            if (actionCount > 1) {
                laik_aseq_addRBufRecv(as, 3 * ba->round + 1,
                                      bufID, bufOff * elemsize,
                                      count, rank);
                laik_aseq_addCopyFromRBuf(as, 3 * ba->round + 2,
                                          ce + rangeOff,
                                          bufID, 0,
                                          actionCount);
                int oldRangeOff = rangeOff;
                for(int k = i; k < as->actionCount; k++) {
                    Laik_BackendAction* ba2 = &(as->action[k]);
                    if (!isSameBufRecv(ba2, round, rank)) continue;

                    assert(rangeOff < copyRanges);
                    ce[rangeOff].ptr = ba2->toBuf;
                    ce[rangeOff].bytes = ba2->count * elemsize;
                    ce[rangeOff].offset = bufOff * elemsize;
                    bufOff += ba2->count;
                    rangeOff++;
                }
                assert(oldRangeOff + actionCount == rangeOff);
            }
            else
                laik_aseq_addBufRecv(as, 3 * ba->round + 1,
                                     ba->toBuf, count, rank);
            break;
        }

        case LAIK_AT_GroupReduce: {
            if (!combineGroupReduce) {
                // pass through
                laik_aseq_addGroupReduce(as, 3 * ba->round + 1,
                                         ba->inputGroup, ba->outputGroup,
                                         ba->fromBuf, ba->toBuf,
                                         ba->count, ba->redOp);
                break;
            }

            int round = ba->round;
            int iGroup = ba->inputGroup;
            int oGroup = ba->outputGroup;
            Laik_ReductionOperation redOp = ba->redOp;
            int count = ba->count;
            int actionCount = 1;
            for(int j = i+1; j < as->actionCount; j++) {
                Laik_BackendAction* ba2 = &(as->action[j]);
                if (!isSameGroupReduce(ba2, round, iGroup, oGroup, redOp))
                    continue;

                ba2->mark = 1;
                count += ba2->count;
                actionCount++;
            }
            if (actionCount > 1) {
                // temporary buffer used as input and output for reduce
                int startBufOff = bufOff;

                // if I provide input: copy pieces into temporary buffer
                if (laik_trans_isInGroup(tc->transition, iGroup, myid)) {
                    laik_aseq_addCopyToRBuf(as, 3 * round,
                                            ce + rangeOff,
                                            bufID, 0,
                                            actionCount);
                    // ranges for input pieces
                    int oldRangeOff = rangeOff;
                    for(int k = i; k < as->actionCount; k++) {
                        Laik_BackendAction* ba2 = &(as->action[k]);
                        if (!isSameGroupReduce(ba2, round, iGroup, oGroup, redOp))
                            continue;

                        assert(rangeOff < copyRanges);
                        ce[rangeOff].ptr = ba2->fromBuf;
                        ce[rangeOff].bytes = ba2->count * elemsize;
                        ce[rangeOff].offset = bufOff * elemsize;
                        bufOff += ba2->count;
                        rangeOff++;
                    }
                    assert(oldRangeOff + actionCount == rangeOff);
                }

                // use temporary buffer for both input and output
                laik_aseq_addRBufGroupReduce(as, 3 * round + 1,
                                             iGroup, oGroup,
                                             bufID, startBufOff * elemsize,
                                             count, redOp);

                // if I want output: copy pieces from temporary buffer
                if (laik_trans_isInGroup(tc->transition, oGroup, myid)) {
                    laik_aseq_addCopyFromRBuf(as, 3 * round + 2,
                                              ce + rangeOff,
                                              bufID, 0,
                                              actionCount);
                    bufOff = startBufOff;
                    int oldRangeOff = rangeOff;
                    for(int k = i; k < as->actionCount; k++) {
                        Laik_BackendAction* ba2 = &(as->action[k]);
                        if (!isSameGroupReduce(ba2, round, iGroup, oGroup, redOp))
                            continue;

                        assert(rangeOff < copyRanges);
                        ce[rangeOff].ptr = ba2->toBuf;
                        ce[rangeOff].bytes = ba2->count * elemsize;
                        ce[rangeOff].offset = bufOff * elemsize;
                        bufOff += ba2->count;
                        rangeOff++;
                    }
                    assert(oldRangeOff + actionCount == rangeOff);
                }
                bufOff = startBufOff + count;
            }
            else
                laik_aseq_addGroupReduce(as, 3 * ba->round + 1,
                                         ba->inputGroup, ba->outputGroup,
                                         ba->fromBuf, ba->toBuf,
                                         ba->count, ba->redOp);
            break;
        }

        case LAIK_AT_Reduce: {
            if (!combineReduce) {
                // pass through
                laik_aseq_addReduce(as, 3 * ba->round + 1,
                                    ba->fromBuf, ba->toBuf,
                                    ba->count, ba->peer_rank, ba->redOp);
                break;
            }

            int round = ba->round;
            int root = ba->peer_rank;
            Laik_ReductionOperation redOp = ba->redOp;
            int count = ba->count;
            int actionCount = 1;
            for(int j = i+1; j < as->actionCount; j++) {
                Laik_BackendAction* ba2 = &(as->action[j]);
                if (!isSameReduce(ba2, round, root, redOp)) continue;

                ba2->mark = 1;
                count += ba2->count;
                actionCount++;
            }
            if (actionCount > 1) {
                // temporary buffer used as input and output for reduce
                int startBufOff = bufOff;

                // copy input pieces into temporary buffer
                laik_aseq_addCopyToRBuf(as, 3 * ba->round,
                                        ce + rangeOff,
                                        bufID, 0,
                                        actionCount);
                // ranges for input pieces
                int oldRangeOff = rangeOff;
                for(int k = i; k < as->actionCount; k++) {
                    Laik_BackendAction* ba2 = &(as->action[k]);
                    if (!isSameReduce(ba2, round, root, redOp)) continue;

                    assert(rangeOff < copyRanges);
                    ce[rangeOff].ptr = ba2->fromBuf;
                    ce[rangeOff].bytes = ba2->count * elemsize;
                    ce[rangeOff].offset = bufOff * elemsize;
                    bufOff += ba2->count;
                    rangeOff++;
                }
                assert(oldRangeOff + actionCount == rangeOff);
                assert(startBufOff + count == bufOff);

                // use temporary buffer for both input and output
                laik_actions_addRBufReduce(as, 3 * round + 1,
                                           bufID, startBufOff * elemsize,
                                           count, root, redOp);

                // if I want result, copy output ranges
                if ((root == myid) || (root == -1)) {
                    // collect output ranges: we cannot reuse copy ranges from
                    // input pieces because of potentially other output buffers
                    laik_aseq_addCopyFromRBuf(as, 3 * round + 2,
                                              ce + rangeOff,
                                              bufID, 0,
                                              actionCount);
                    bufOff = startBufOff;
                    int oldRangeOff = rangeOff;
                    for(int k = i; k < as->actionCount; k++) {
                        Laik_BackendAction* ba2 = &(as->action[k]);
                        if (!isSameReduce(ba2, round, root, redOp)) continue;

                        assert(rangeOff < copyRanges);
                        ce[rangeOff].ptr = ba2->toBuf;
                        ce[rangeOff].bytes = ba2->count * elemsize;
                        ce[rangeOff].offset = bufOff * elemsize;
                        bufOff += ba2->count;
                        rangeOff++;
                    }
                    assert(oldRangeOff + actionCount == rangeOff);
                }
                bufOff = startBufOff + count;
            }
            else
                laik_aseq_addReduce(as, 3 * ba->round + 1,
                                    ba->fromBuf, ba->toBuf,
                                    ba->count, ba->peer_rank, ba->redOp);
            break;
        }

        default:
            // pass through
            laik_aseq_add(ba, as, 3 * ba->round + 1);
            break;
        }
    }
    assert(rangeOff == copyRanges);
    assert(bufSize == bufOff);

    laik_aseq_activateNewActions(as);

    return true;
}


// helpers for action resorting

// helper for action sorting if it sorts by rounds:
// add actions already sorted in pointer array <order> to <as>, compress rounds
static
void addResorted(int count, Laik_BackendAction** order, Laik_ActionSeq* as)
{
    assert(count > 0);

    // add actions in new order to as2, compress rounds
    int round;
    int oldRound = order[0]->round;
    int newRound = 0;
    for(int i = 0; i < count; i++) {
        round = order[i]->round;
        assert(round >= oldRound); // must be sorted
        if (round > oldRound) {
            oldRound = round;
            newRound++;
        }
        laik_aseq_add(order[i], as, newRound);
    }
}

// used by compare functions, set directly before sort
static int myid4cmp;

static
int cmp2phase(const void* aptr1, const void* aptr2)
{
    Laik_BackendAction* ba1 = *((Laik_BackendAction**) aptr1);
    Laik_BackendAction* ba2 = *((Laik_BackendAction**) aptr2);

    if (ba1->round != ba2->round)
        return ba1->round - ba2->round;

    bool a1isSend = laik_action_isSend(ba1);
    bool a2isSend = laik_action_isSend(ba2);
    bool a1isRecv = laik_action_isRecv(ba1);
    bool a2isRecv = laik_action_isRecv(ba2);
    int a1peer = ba1->peer_rank;
    int a2peer = ba2->peer_rank;

    int a1phase = 0;
    if (a1isRecv) a1phase = (a1peer < myid4cmp) ? 1 : 4;
    if (a1isSend) a1phase = (a1peer < myid4cmp) ? 3 : 2;

    int a2phase = 0;
    if (a2isRecv) a2phase = (a2peer < myid4cmp) ? 1 : 4;
    if (a2isSend) a2phase = (a2peer < myid4cmp) ? 3 : 2;

    // if different phases, sort by phase number
    // i.e. all send/recv actions will come last in a round
    if (a1phase != a2phase)
        return a1phase - a2phase;

    if (a1phase > 0) {
        // within a send/recv phase, sort actions by peer ranks
        if (a1peer != a2peer)
            return a1peer - a2peer;

        // with same peers, use original order
        // we can compare pointers to actions (as they are not sorted directly!)
        return (int) (ba1 - ba2);
    }

    // both are neither send/recv actions: keep same order
    // we can compare pointers to actions (as they are not sorted directly!)
    return (int) (ba1 - ba2);
}

/* sort actions into 2 phases to avoid deadlocks
 * (1) messages from lower to higher ranks
 *     - receive from lower rank <X>
 *     - send to higher rank <X>
 * (2) messages from higher to lower ranks
 *     - send to lower rank <X>
 *     - receive from higher rank <X>
 * for sends/recvs among same peers, order must be kept
 *
 * Actions other the send/recv are moved to front.
 * Copy resorted sequence into as2.
 */
bool laik_aseq_sort_2phases(Laik_ActionSeq* as)
{
    if (as->actionCount == 0) return false;

    // must not have new actions, we want to start a new build
    assert(as->newActionCount == 0);

    Laik_BackendAction** order = malloc(as->actionCount * sizeof(void*));
    for(int i=0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);
        order[i] = ba;
    }

    Laik_TransitionContext* tc = as->context[0];
    myid4cmp = tc->transition->group->myid;
    qsort(order, as->actionCount, sizeof(void*), cmp2phase);

    // check if something changed
    bool changed = false;
    for(int i=0; i < as->actionCount; i++) {
        if (order[i] == &(as->action[i])) continue;
        changed = true;
        break;
    }
    if (changed) {
        addResorted(as->actionCount, order, as);
        laik_aseq_activateNewActions(as);
    }
    free(order);

    return changed;
}

static
int cmp_rankdigits(const void* aptr1, const void* aptr2)
{
    Laik_BackendAction* ba1 = *((Laik_BackendAction**) aptr1);
    Laik_BackendAction* ba2 = *((Laik_BackendAction**) aptr2);

    if (ba1->round != ba2->round)
        return ba1->round - ba2->round;

    bool a1isSend = laik_action_isSend(ba1);
    bool a2isSend = laik_action_isSend(ba2);
    bool a1isRecv = laik_action_isRecv(ba1);
    bool a2isRecv = laik_action_isRecv(ba2);
    int a1peer = ba1->peer_rank;
    int a2peer = ba2->peer_rank;

    // phase number is number of lower digits equal to my rank
    int a1phase = 0, a2phase = 0, mask;
    if (a1isSend || a1isRecv)
        for(a1phase = 1, mask = 1; mask; a1phase++, mask = mask << 1)
            if ((a1peer & mask) != (myid4cmp & mask)) break;
    if (a2isSend || a2isRecv)
        for(a2phase = 1, mask = 1; mask; a2phase++, mask = mask << 1)
            if ((a2peer & mask) != (myid4cmp & mask)) break;

    // if different phases, sort by phase number
    if (a1phase != a2phase)
        return a1phase - a2phase;

    if (a1phase > 0) {
        // calculate sub-phase (we reuse the variables for subphase)
        if ((myid4cmp & mask) == 0) {
            // sub-phase: with first diverging bit = 0, we first send
            a1phase = a1isSend ? 1 : 2;
            a2phase = a2isSend ? 1 : 2;
        }
        else {
            a1phase = a1isSend ? 2 : 1;
            a2phase = a2isSend ? 2 : 1;
        }
        // if different sub-phases, sort by sub-phase number
        if (a1phase != a2phase)
            return a1phase - a2phase;

        // within same sub-phase, sort actions by peer ranks
        if (a1peer != a2peer)
            return a1peer - a2peer;
    }
    // otherwise, keep original order
    // we can compare pointers to actions (as they are not sorted directly!)
    return (int) (ba1 - ba2);
}

/* sort actions using binary digits of rank numbers, to avoid deadlocks
 * (1) use bit 0 of peer rank and myself
 *     - ranks with bit0 = 0 send to ranks with bit0 = 1
 *     - ranks with bit0 = 1 send to ranks with bit0 = 0
 * (2) use bit 1
 * ...
 * for sends/recvs among same peers, order must be kept
 *
 * Actions other the send/recv are moved to front.
 * Copy resorted sequence into as2.
 */
bool laik_aseq_sort_rankdigits(Laik_ActionSeq* as)
{
    if (as->actionCount == 0) return false;

    // must not have new actions, we want to start a new build
    assert(as->newActionCount == 0);

    Laik_BackendAction** order = malloc(as->actionCount * sizeof(void*));
    for(int i=0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);
        order[i] = ba;
    }

    Laik_TransitionContext* tc = as->context[0];
    myid4cmp = tc->transition->group->myid;
    qsort(order, as->actionCount, sizeof(void*), cmp_rankdigits);

    // check if something changed
    bool changed = false;
    for(int i=0; i < as->actionCount; i++) {
        if (order[i] == &(as->action[i])) continue;
        changed = true;
        break;
    }
    if (changed) {
        addResorted(as->actionCount, order, as);
        laik_aseq_activateNewActions(as);
    }
    free(order);

    return changed;
}


// helper for just sorting by rounds

static
int cmp_rounds(const void* aptr1, const void* aptr2)
{
    Laik_BackendAction* ba1 = *((Laik_BackendAction**) aptr1);
    Laik_BackendAction* ba2 = *((Laik_BackendAction**) aptr2);

    if (ba1->round != ba2->round)
        return ba1->round - ba2->round;

    // otherwise, keep original order
    // we can compare pointers to actions (as they are not sorted directly!)
    return (int) (ba1 - ba2);
}

// sort actions according to their rounds, and compress rounds
bool laik_aseq_sort_rounds(Laik_ActionSeq* as)
{
    if (as->actionCount == 0) return false;

    // must not have new actions, we want to start a new build
    assert(as->newActionCount == 0);

    Laik_BackendAction** order = malloc(as->actionCount * sizeof(void*));
    for(int i=0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);
        order[i] = ba;
    }

    qsort(order, as->actionCount, sizeof(void*), cmp_rounds);

    // check if something changed
    bool changed = false;
    for(int i=0; i < as->actionCount; i++) {
        if (order[i] == &(as->action[i])) continue;
        changed = true;
        break;
    }
    if (changed) {
        addResorted(as->actionCount, order, as);
        laik_aseq_activateNewActions(as);
    }
    free(order);

    return changed;
}



/*
 * transform MapPackAndSend/MapRecvAndUnpack into simple Send/Recv actions
 * if mapping is known and direct send/recv is possible
 *
 * we enforce the following rounds:
 * - round 0: eventually pack from container to buffer
 * - round 1: send/recv messages
 * - round 2: eventually unpack from buffer into container
 * All action round numbers are spreaded by *3+1, allowing space for added
 * pack/unpack copy actions before/after.
 *
 * return true if action sequence changed
*/
bool laik_aseq_flattenPacking(Laik_ActionSeq* as)
{
    bool changed = false;

    Laik_Mapping *fromMap, *toMap;
    int64_t from, to;

    // must not have new actions, we want to start a new build
    assert(as->newActionCount == 0);

    Laik_TransitionContext* tc = as->context[0];
    int elemsize = tc->data->elemsize;
    int myid = tc->transition->group->myid;

    for(int i = 0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);
        bool handled = false;

        switch(ba->type) {
        case LAIK_AT_MapPackAndSend:
            if (tc->fromList)
                assert(ba->fromMapNo < tc->fromList->count);
            fromMap = tc->fromList ? &(tc->fromList->map[ba->fromMapNo]) : 0;

            if (fromMap && (ba->dims == 1)) {
                // mapping known and 1d: can use direct send/recv

                // FIXME: this assumes lexicographical layout
                from = ba->slc->from.i[0] - fromMap->requiredSlice.from.i[0];
                to   = ba->slc->to.i[0] - fromMap->requiredSlice.from.i[0];
                assert(from >= 0);
                assert(to > from);

                // replace with different action depending on map allocation done
                if (fromMap->base)
                    laik_aseq_addBufSend(as, 3 * ba->round + 1,
                                         fromMap->base + from * elemsize,
                                         to - from, ba->peer_rank);
                else
                    laik_aseq_addMapSend(as, 3 * ba->round + 1,
                                         ba->fromMapNo, from * elemsize,
                                         to - from, ba->peer_rank);
            }
            else {
                // split off packing and sending, using a buffer of required size
                int bufID = laik_aseq_addBufReserve(as, ba->count * elemsize, -1);
                if (fromMap)
                    laik_aseq_addPackToRBuf(as, 3 * ba->round,
                                            fromMap, ba->slc, bufID, 0);
                else
                    laik_aseq_addMapPackToRBuf(as, 3 * ba->round,
                                               ba->fromMapNo, ba->slc, bufID, 0);

                laik_aseq_addRBufSend(as, 3 * ba->round + 1,
                                      bufID, 0, ba->count, ba->peer_rank);
            }
            handled = true;
            break;

        case LAIK_AT_MapRecvAndUnpack:
            if (tc->toList)
                assert(ba->toMapNo < tc->toList->count);
            toMap = tc->toList ? &(tc->toList->map[ba->toMapNo]) : 0;

            if (toMap && (ba->dims == 1)) {
                // mapping known and 1d: can use direct send/recv

                // FIXME: this assumes lexicographical layout
                from = ba->slc->from.i[0] - toMap->requiredSlice.from.i[0];
                to   = ba->slc->to.i[0] - toMap->requiredSlice.from.i[0];
                assert(from >= 0);
                assert(to > from);

                // replace with different action depending on map allocation done
                if (toMap->base)
                    laik_aseq_addBufRecv(as, 3 * ba->round + 1,
                                         toMap->base + from * elemsize,
                                         to - from, ba->peer_rank);
                else
                    laik_aseq_addMapRecv(as, 3 * ba->round + 1,
                                         ba->toMapNo, from * elemsize,
                                         to - from, ba->peer_rank);
            }
            else {
                // split off receiving and unpacking, using buffer of required size
                int bufID = laik_aseq_addBufReserve(as, ba->count * elemsize, -1);
                laik_aseq_addRBufRecv(as, 3 * ba->round + 1,
                                      bufID, 0, ba->count, ba->peer_rank);
                if (toMap)
                    laik_aseq_addUnpackFromRBuf(as, 3 * ba->round + 2,
                                                bufID, 0, toMap, ba->slc);
                else
                    laik_aseq_addMapUnpackFromRBuf(as, 3 * ba->round + 2,
                                                   bufID, 0, ba->toMapNo, ba->slc);
            }
            handled = true;
            break;

        case LAIK_AT_MapGroupReduce:

            // TODO: for >1 dims, use pack/unpack with buffer
            if (ba->dims == 1) {
                char *fromBase, *toBase;

                // if current task is input, fromBase should be allocated
                if (laik_trans_isInGroup(tc->transition, ba->inputGroup, myid)) {
                    assert(tc->fromList);
                    assert(ba->fromMapNo < tc->fromList->count);
                    fromMap = &(tc->fromList->map[ba->fromMapNo]);
                    fromBase = fromMap ? fromMap->base : 0;
                    assert(fromBase != 0);
                }
                else
                    fromBase = 0;

                // if current task is receiver, toBase should be allocated
                if (laik_trans_isInGroup(tc->transition, ba->outputGroup, myid)) {
                    assert(tc->toList);
                    assert(ba->toMapNo < tc->toList->count);
                    toMap = &(tc->toList->map[ba->toMapNo]);
                    toBase = toMap ? toMap->base : 0;
                    assert(toBase != 0);
                }
                else
                    toBase = 0; // no interest in receiving anything

                // FIXME: this assumes lexicographical layout
                from = ba->slc->from.i[0];
                to   = ba->slc->to.i[0];
                assert(to > from);

                if (fromBase) {
                    assert(from >= fromMap->requiredSlice.from.i[0]);
                    fromBase += (from - fromMap->requiredSlice.from.i[0]) * elemsize;
                }
                if (toBase) {
                    assert(from >= toMap->requiredSlice.from.i[0]);
                    toBase += (from - toMap->requiredSlice.from.i[0]) * elemsize;
                }

                laik_aseq_addGroupReduce(as, 3 * ba->round + 1,
                                         ba->inputGroup, ba->outputGroup,
                                         fromBase, toBase, to - from, ba->redOp);
                handled = true;
            }
            break;

        default: break;
        }

        if (!handled)
            laik_aseq_add(ba, as, 3 * ba->round + 1);
        else
            changed = true;
    }

    if (changed)
        laik_aseq_activateNewActions(as);
    else
        laik_aseq_discardNewActions(as);

    return changed;
}

// helpers for splitReduce transformation

// add actions for 3-step manual reduction for a group-reduce action
// spread rounds by 3
// round 0: send to reduce task, round 1: reduction, round 2: send back
static
void laik_aseq_addReduce3Rounds(Laik_ActionSeq* as,
                                Laik_TransitionContext* tc, Laik_BackendAction* a)
{
    assert(a->type == LAIK_AT_GroupReduce);
    Laik_Transition* t = tc->transition;
    Laik_Data* data = tc->data;

    // do the manual reduction on smallest rank of output group
    int reduceTask = laik_trans_taskInGroup(t, a->outputGroup, 0);
    int myid = t->group->myid;

    if (myid != reduceTask) {
        // not the reduce task: eventually send input and recv result

        if (laik_trans_isInGroup(t, a->inputGroup, myid)) {
            // send action in round 0
            laik_aseq_addBufSend(as, 3 * a->round,
                                 a->fromBuf, a->count, reduceTask);
        }

        if (laik_trans_isInGroup(t, a->outputGroup, myid)) {
            // recv action only in round 2
            laik_aseq_addBufRecv(as, 3 * a->round + 2,
                                 a->toBuf, a->count, reduceTask);
        }

        return;
    }

    // we are the reduce task

    int inCount = laik_trans_groupCount(t, a->inputGroup);
    uint64_t byteCount = a->count * data->elemsize;
    bool inputFromMe = laik_trans_isInGroup(t, a->inputGroup, myid);

    // buffer for all partial input values
    int bufSize = (inCount - (inputFromMe ? 1:0)) * byteCount;
    int bufID = -1;
    if (bufSize > 0)
        bufID = laik_aseq_addBufReserve(as, bufSize, -1);

    // collect values from tasks in input group
    int bufOff[32], off = 0;
    assert(inCount <= 32); // TODO: support more than 32 partitial inputs

    // always put this task in front: we use toBuf to calculate
    // our results, but there may be input from us, which would
    // be overwritten if not starting with our input
    int ii = 0;
    if (inputFromMe) {
        ii++; // slot 0 reserved for this task (use a->fromBuf)
        bufOff[0] = 0;
    }
    for(int i = 0; i < inCount; i++) {
        int inTask = laik_trans_taskInGroup(t, a->inputGroup, i);
        if (inTask == myid) continue;

        laik_aseq_addRBufRecv(as, 3 * a->round,
                              bufID, off, a->count, inTask);
        bufOff[ii++] = off;
        off += byteCount;
    }
    assert(ii == inCount);
    assert(off == bufSize);

    if (inCount == 0) {
        // no input: add init action for neutral element of reduction
        laik_aseq_addBufInit(as, 3 * a->round + 1,
                             data->type, a->redOp, a->toBuf, a->count);
    }
    else {
        // move first input to a->toBuf, and then reduce on that
        if (inputFromMe) {
            if (a->fromBuf != a->toBuf) {
                // if my input is not already at a->toBuf, copy it
                laik_aseq_addBufCopy(as, 3 * a->round + 1,
                                     a->fromBuf, a ->toBuf, a->count);
            }
        }
        else {
            // copy first input to a->toBuf
            laik_aseq_addRBufCopy(as,  3 * a->round + 1,
                                  bufID, bufOff[0], a->toBuf, a->count);
        }

        // do reduction with other inputs
        for(int t = 1; t < inCount; t++)
            laik_aseq_addRBufLocalReduce(as, 3 * a->round + 1,
                                         data->type, a->redOp,
                                         bufID, bufOff[t],
                                         a->toBuf, a->count);
    }

    // send result to tasks in output group
    int outCount = laik_trans_groupCount(t, a->outputGroup);
    for(int i = 0; i< outCount; i++) {
        int outTask = laik_trans_taskInGroup(t, a->outputGroup, i);
        if (outTask == myid) {
            // that's myself: nothing to do
            continue;
        }

        laik_aseq_addBufSend(as,  3 * a->round + 2,
                             a->toBuf, a->count, outTask);
    }
}

// add actions for 2-step manual reduction for a group-reduce action
// intermixed with 3-step reduction, thus need to spread rounds by 3
// round 0: send/recv, round 1: reduction
static
void laik_aseq_addReduce2Rounds(Laik_ActionSeq* as,
                                Laik_TransitionContext* tc, Laik_BackendAction* a)
{
    assert(a->type == LAIK_AT_GroupReduce);
    Laik_Transition* t = tc->transition;
    Laik_Data* data = tc->data;

    // everybody sends his input to all others, everybody does reduction
    int myid = t->group->myid;

    bool inputFromMe = laik_trans_isInGroup(t, a->inputGroup, myid);
    if (inputFromMe) {
        // send my input to all tasks in output group
        int outCount = laik_trans_groupCount(t, a->outputGroup);
        for(int i = 0; i< outCount; i++) {
            int outTask = laik_trans_taskInGroup(t, a->outputGroup, i);
            if (outTask == myid) {
                // that's myself: nothing to do
                continue;
            }

            laik_aseq_addBufSend(as,  3 * a->round,
                                 a->fromBuf, a->count, outTask);
        }
    }

    if (!laik_trans_isInGroup(t, a->outputGroup, myid)) return;

    // I am interested in result, process inputs from others

    int inCount = laik_trans_groupCount(t, a->inputGroup);
    uint64_t byteCount = a->count * data->elemsize;
    // buffer for all partial input values
    int bufSize = (inCount - (inputFromMe ? 1:0)) * byteCount;
    int bufID = -1;
    if (bufSize > 0)
        bufID = laik_aseq_addBufReserve(as, bufSize, -1);

    // collect values from tasks in input group
    int bufOff[32], off = 0;
    assert(inCount <= 32); // TODO: support more than 32 partitial inputs

    // always put this task in front: we use toBuf to calculate
    // our results, but there may be input from us, which would
    // be overwritten if not starting with our input
    int ii = 0;
    if (inputFromMe) {
        ii++; // slot 0 reserved for this task (use a->fromBuf)
        bufOff[0] = 0;
    }
    for(int i = 0; i < inCount; i++) {
        int inTask = laik_trans_taskInGroup(t, a->inputGroup, i);
        if (inTask == myid) continue;

        laik_aseq_addRBufRecv(as, 3 * a->round,
                              bufID, off, a->count, inTask);
        bufOff[ii++] = off;
        off += byteCount;
    }
    assert(ii == inCount);
    assert(off == bufSize);

    if (inCount == 0) {
        // no input: add init action for neutral element of reduction
        laik_aseq_addBufInit(as, 3 * a->round + 1,
                             data->type, a->redOp, a->toBuf, a->count);
    }
    else {
        // move first input to a->toBuf, and then reduce on that
        if (inputFromMe) {
            if (a->fromBuf != a->toBuf) {
                // if my input is not already at a->toBuf, copy it
                laik_aseq_addBufCopy(as, 3 * a->round +1,
                                     a->fromBuf, a ->toBuf, a->count);
            }
        }
        else {
            // copy first input to a->toBuf
            laik_aseq_addRBufCopy(as, 3 * a->round + 1,
                                  bufID, bufOff[0], a->toBuf, a->count);
        }

        // do reduction with other inputs
        for(int t = 1; t < inCount; t++)
            laik_aseq_addRBufLocalReduce(as, 3 * a->round +1,
                                         data->type, a->redOp,
                                         bufID, bufOff[t],
                                         a->toBuf, a->count);
    }
}

// transformation for split reduce actions into basic multiple actions.
// action round numbers are spreaded by *3+1, allowing space for 3-step
// return true if sequence changed
bool laik_aseq_splitReduce(Laik_ActionSeq* as)
{
    bool reduceFound = false;

    // must not have new actions, we want to start a new build
    assert(as->newActionCount == 0);

    Laik_TransitionContext* tc = as->context[0];

    for(int i = 0; i < as->actionCount; i++) {
        if (as->action[i].type == LAIK_AT_GroupReduce) {
            reduceFound = true;
            break;
        }
    }

    if (!reduceFound)
        return false;

    for(int i = 0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);

        switch(ba->type) {
        case LAIK_AT_GroupReduce: {
            int inCount, outCount;
            inCount = laik_trans_groupCount(tc->transition, ba->inputGroup);
            outCount = laik_trans_groupCount(tc->transition, ba->inputGroup);
            // use simple 3-step reduction if too many messages for 2-step
            if (inCount * outCount > 4 * (inCount + outCount))
                laik_aseq_addReduce3Rounds(as, tc, ba);
            else
                laik_aseq_addReduce2Rounds(as, tc, ba);
            break;
        }

        default:
            laik_aseq_add(ba, as, 3 * ba->round + 1);
            break;
        }
    }

    laik_aseq_activateNewActions(as);
    return true;
}

// replace transition exec actions with equivalent reduce/send/recv actions
bool laik_aseq_splitTransitionExecs(Laik_ActionSeq* as)
{
    // must not have new actions, we want to start a new build
    assert(as->newActionCount == 0);

    Laik_TransitionContext* tc = as->context[0];
    bool found = false;
    for(int i = 0; i < as->actionCount; i++) {
        if (as->action[i].type == LAIK_AT_TExec) {
            found = true;
            break;
        }
    }
    if (!found)
        return false;

    for(int i = 0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);

        switch(ba->type) {
        case LAIK_AT_TExec:
            assert(ba->tid == 0);
            laik_aseq_addReds(as, ba->round, tc->data, tc->transition);
            laik_aseq_addSends(as, ba->round, tc->data, tc->transition);
            laik_aseq_addRecvs(as, ba->round, tc->data, tc->transition);
            break;

        default:
            laik_aseq_add(ba, as, -1);
            break;
        }
    }

    laik_aseq_activateNewActions(as);
    return true;
}

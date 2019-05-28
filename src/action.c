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
#include <stdio.h>

static int aseq_id = 0;

// create a new action sequence object, usable for the given LAIK instance
Laik_ActionSeq* laik_aseq_new(Laik_Instance *inst)
{
    Laik_ActionSeq* as = malloc(sizeof(Laik_ActionSeq));
    if (!as) {
        laik_panic("Out of memory allocating Laik_ActionSeq object");
        exit(1); // not actually needed, laik_panic never returns
    }
    as->id = aseq_id++;
    as->name = strdup("aseq-0     ");
    sprintf(as->name, "aseq-%d", as->id);

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
    as->ceRanges = 0;

    as->actionCount = 0;
    as->bytesUsed = 0;
    as->action = 0;
    as->roundCount = 0;

    as->newAction = 0;
    as->newActionCount = 0;
    as->newBytesUsed = 0;
    as->newBytesAlloc = 0;
    as->newRoundCount = 0;

    // mark that stats are not yet calculated
    as->transitionCount = 0;

    laik_log(1, "new action seq '%s'", as->name);

    return as;
}

// free all resources allocated for an action sequence
// this may include backend-specific resources
void laik_aseq_free(Laik_ActionSeq* as)
{
    if (!as) return;

    laik_log(1, "free action seq '%s' (%d actions, %d buffers)",
             as->name, as->actionCount, as->bufferCount);

    if (as->backend) {
        // ask backend to do its own cleanup for this action sequence
        (as->backend->cleanup)(as);
    }

    Laik_TransitionContext* tc = as->context[0];

    for(int i = 0; i < as->bufferCount; i++) {
        if (as->bufSize[i] == 0) continue;

        laik_log(1, "    free buffer %d: %lu bytes\n", i, as->bufSize[i]);
        free(as->buf[i]);

        // update allocation statistics
        laik_switchstat_free(tc->data->stat, as->bufSize[i]);
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
Laik_Action* laik_aseq_addAction(Laik_ActionSeq* as, unsigned int size,
                                 Laik_ActionType type, int round, int tid)
{
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

    assert(type < 256);
    assert(size < 256);
    assert(round < 256);
    assert(tid < 128);
    a->type  = (unsigned char) type;
    a->len   = (unsigned char) size;
    a->round = (unsigned char) round;
    a->tid   = (unsigned char) tid;
    a->mark  = 0;

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
    as->bytesUsed = as->newBytesUsed;
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
                                                   LAIK_AT_Invalid, round, 0);
    return ba;
}

int laik_aseq_addTContext(Laik_ActionSeq* as,
                          Laik_Data* data, Laik_Transition* transition,
                          Laik_MappingList* fromList,
                          Laik_MappingList* toList)
{
    // the transition must be valid
    assert(transition != 0);

    Laik_TransitionContext* tc = malloc(sizeof(Laik_TransitionContext));
    tc->data = data;
    tc->transition = transition;
    tc->fromList = fromList;
    tc->toList = toList;
    tc->prepFromList = 0;
    tc->prepToList = 0;

    assert(as->contextCount < ASEQ_CONTEXTS_MAX);
    int contextID = as->contextCount;
    as->contextCount++;

    assert(as->context[contextID] == 0);
    as->context[contextID] = tc;

    laik_log(1, "action seq '%s': added context for trans '%s' on data '%s'",
             as->name, transition->name, data->name);

    return contextID;
}

// append action to stop execution (even if there are more in the sequence)
void laik_aseq_addHalt(Laik_ActionSeq* as)
{
    // Halt does not need more size then Laik_Action
    laik_aseq_addAction(as, sizeof(Laik_Action), LAIK_At_Halt, 0, 0);
}

// append action to do the transition specified by the transition context ID
// call laik_aseq_addTContext() before to add a context and get the ID.
void laik_aseq_addTExec(Laik_ActionSeq* as, int tid)
{
    // TExec does not need more size then Laik_Action
    laik_aseq_addAction(as, sizeof(Laik_Action), LAIK_AT_TExec, 0, tid);
}


// append action to reserve buffer space
// if <bufID> is negative, a new ID is generated (always > 100)
// returns bufID.
//
// bufID < 100 are reserved for buffers already allocated (buf[bufID]).
// in a final pass, all buffer reservations must be collected, the buffer
// allocated (with ID 0), and the references to this buffer replaced
// by references into buffer 0. These actions can be removed afterwards.
int laik_aseq_addBufReserve(Laik_ActionSeq* as, unsigned int size, int bufID)
{
    if (bufID < 0) {
        // generate new buf ID
        // only return IDs > 100. ID <100 are reserved for actual buffers
        bufID = (int)(as->bufReserveCount + 100);
        as->bufReserveCount++;
    }
    else
        assert(bufID <= (int)(as->bufReserveCount + 99));

    // BufReserve in round 0: allocation is done before exec
    Laik_A_BufReserve* a;
    a = (Laik_A_BufReserve*) laik_aseq_addAction(as, sizeof(*a),
                                                 LAIK_AT_BufReserve, 0, 0);
    a->size = size;
    a->bufID = bufID;
    a->offset = 0;

    return bufID;
}

// append send action to buffer referencing a previous reserve action
void laik_aseq_addRBufSend(Laik_ActionSeq* as, int round,
                           int bufID, unsigned int byteOffset,
                           unsigned int count, int to)
{
    Laik_A_RBufSend* a;
    a = (Laik_A_RBufSend*) laik_aseq_addAction(as, sizeof(*a),
                                               LAIK_AT_RBufSend, round, 0);
    a->bufID = bufID;
    a->offset = byteOffset;
    a->count = count;
    a->to_rank = to;
}

// append recv action into buffer referencing a previous reserve action
void laik_aseq_addRBufRecv(Laik_ActionSeq* as, int round,
                           int bufID, unsigned int byteOffset,
                           unsigned int count, int from)
{
    Laik_A_RBufRecv* a;
    a = (Laik_A_RBufRecv*) laik_aseq_addAction(as, sizeof(*a),
                                               LAIK_AT_RBufRecv, round, 0);
    a->bufID = bufID;
    a->offset = byteOffset;
    a->count = count;
    a->from_rank = from;
}

// append action to call a local reduce operation
// using buffer referenced by a previous reserve action and toBuf as input
void laik_aseq_addRBufLocalReduce(Laik_ActionSeq* as, int round,
                                  Laik_Type* dtype,
                                  Laik_ReductionOperation redOp,
                                  int fromBufID, unsigned int fromByteOffset,
                                  char* toBuf, unsigned int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->h.type = LAIK_AT_RBufLocalReduce;
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
                          char* toBuf, unsigned int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->h.type = LAIK_AT_BufInit;
    a->dtype = dtype;
    a->redOp = redOp;
    a->toBuf = toBuf;
    a->count = count;
}

// append action to call a copy operation from/to a buffer
// if fromBuf is 0, use a buffer referenced by a previous reserve action
void laik_aseq_addRBufCopy(Laik_ActionSeq* as, int round,
                           int fromBufID, unsigned int fromByteOffset,
                           char* toBuf, unsigned int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->h.type = LAIK_AT_RBufCopy;
    a->bufID = fromBufID;
    a->offset = fromByteOffset;
    a->toBuf = toBuf;
    a->count = count;
}

// append action to call a copy operation from/to a buffer
void laik_aseq_addBufCopy(Laik_ActionSeq* as, int round,
                          char* fromBuf, char* toBuf, unsigned int count)
{
    assert(fromBuf != toBuf);

    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->h.type = LAIK_AT_BufCopy;
    a->fromBuf = fromBuf;
    a->toBuf = toBuf;
    a->count = count;
}


// append send action from a mapping with offset
void laik_aseq_addMapSend(Laik_ActionSeq* as, int round,
                          int fromMapNo, unsigned int off,
                          unsigned int count, int to)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->h.type = LAIK_AT_MapSend;
    a->fromMapNo = fromMapNo;
    a->offset = off;
    a->count = count;
    a->rank = to;
}

// append send action from a buffer
void laik_aseq_addBufSend(Laik_ActionSeq* as, int round,
                          char* fromBuf, unsigned int count, int to)
{
    Laik_A_BufSend* a;
    a = (Laik_A_BufSend*) laik_aseq_addAction(as, sizeof(*a),
                                              LAIK_AT_BufSend, round, 0);
    a->buf = fromBuf;
    a->count = count;
    a->to_rank = to;
}

// append recv action into a mapping with offset
void laik_aseq_addMapRecv(Laik_ActionSeq* as, int round,
                          int toMapNo, unsigned int off,
                          unsigned int count, int from)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->h.type = LAIK_AT_MapRecv;
    a->toMapNo = toMapNo;
    a->offset = off;
    a->count = count;
    a->rank = from;
}

// append recv action into a buffer
void laik_aseq_addBufRecv(Laik_ActionSeq* as, int round,
                          char* toBuf, unsigned int count, int from)
{
    Laik_A_BufRecv* a;
    a = (Laik_A_BufRecv*) laik_aseq_addAction(as, sizeof(*a),
                                              LAIK_AT_BufRecv, round, 0);
    a->buf = toBuf;
    a->count = count;
    a->from_rank = from;
}

void laik_aseq_addPackAndSend(Laik_ActionSeq* as, int round,
                              Laik_Mapping* fromMap, Laik_Slice* slc, int to)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->h.type = LAIK_AT_PackAndSend;
    a->map = fromMap;
    a->slc = slc;
    a->rank = to;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}

void laik_aseq_addPackToRBuf(Laik_ActionSeq* as, int round,
                             Laik_Mapping* fromMap, Laik_Slice* slc,
                             int toBufID, unsigned int toByteOffset)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->h.type = LAIK_AT_PackToRBuf;
    a->map = fromMap;
    a->slc = slc;
    a->bufID = toBufID;
    a->offset = toByteOffset;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}

void laik_aseq_addPackToBuf(Laik_ActionSeq* as, int round,
                            Laik_Mapping* fromMap, Laik_Slice* slc, char* toBuf)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->h.type = LAIK_AT_PackToBuf;
    a->map = fromMap;
    a->slc = slc;
    a->toBuf = toBuf;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}

void laik_aseq_addMapPackAndSend(Laik_ActionSeq* as, int round,
                                 int fromMapNo, Laik_Slice* slc, int to)
{
    Laik_A_MapPackAndSend* a;
    a = (Laik_A_MapPackAndSend*) laik_aseq_addAction(as, sizeof(*a),
                                                     LAIK_AT_MapPackAndSend,
                                                     round, 0);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->fromMapNo = fromMapNo;
    a->slc = slc;
    a->to_rank = to;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}

void laik_aseq_addMapPackToRBuf(Laik_ActionSeq* as, int round,
                                int fromMapNo, Laik_Slice* slc,
                                int toBufID, unsigned int toByteOffset)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->h.type = LAIK_AT_MapPackToRBuf;
    a->fromMapNo = fromMapNo;
    a->slc = slc;
    a->bufID = toBufID;
    a->offset = toByteOffset;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}

void laik_aseq_addMapPackToBuf(Laik_ActionSeq* as, int round,
                               int fromMapNo, Laik_Slice* slc, char* toBuf)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->h.type = LAIK_AT_MapPackToBuf;
    a->fromMapNo = fromMapNo;
    a->slc = slc;
    a->toBuf = toBuf;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}

void laik_aseq_addRecvAndUnpack(Laik_ActionSeq* as, int round,
                                Laik_Mapping* toMap, Laik_Slice* slc, int from)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->h.type = LAIK_AT_RecvAndUnpack;
    a->map = toMap;
    a->slc = slc;
    a->rank = from;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}

void laik_aseq_addUnpackFromRBuf(Laik_ActionSeq* as, int round,
                                 int fromBufID, unsigned int fromByteOffset,
                                 Laik_Mapping* toMap, Laik_Slice* slc)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->h.type = LAIK_AT_UnpackFromRBuf;
    a->bufID = fromBufID;
    a->offset = fromByteOffset;
    a->map = toMap;
    a->slc = slc;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}

void laik_aseq_addUnpackFromBuf(Laik_ActionSeq* as, int round,
                                char* fromBuf, Laik_Mapping* toMap, Laik_Slice* slc)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->h.type = LAIK_AT_UnpackFromBuf;
    a->fromBuf = fromBuf;
    a->map = toMap;
    a->slc = slc;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}

void laik_aseq_addMapRecvAndUnpack(Laik_ActionSeq* as, int round,
                                   int toMapNo, Laik_Slice* slc, int from)
{
    Laik_A_MapRecvAndUnpack* a;
    a = (Laik_A_MapRecvAndUnpack*) laik_aseq_addAction(as, sizeof(*a),
                                                       LAIK_AT_MapRecvAndUnpack,
                                                       round, 0);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->toMapNo = toMapNo;
    a->slc = slc;
    a->from_rank = from;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}

void laik_aseq_addMapUnpackFromRBuf(Laik_ActionSeq* as, int round,
                                    int fromBufID, unsigned int fromByteOffset,
                                    int toMapNo, Laik_Slice* slc)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->h.type = LAIK_AT_MapUnpackFromRBuf;
    a->bufID = fromBufID;
    a->offset = fromByteOffset;
    a->toMapNo = toMapNo;
    a->slc = slc;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}

void laik_aseq_addMapUnpackFromBuf(Laik_ActionSeq* as, int round,
                                   char* fromBuf, int toMapNo, Laik_Slice* slc)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->h.type = LAIK_AT_MapUnpackFromBuf;
    a->fromBuf = fromBuf;
    a->toMapNo = toMapNo;
    a->slc = slc;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}


void laik_aseq_addReduce(Laik_ActionSeq* as, int round,
                         char* fromBuf, char* toBuf, unsigned int count,
                         int rootTask, Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    assert(count > 0);

    a->h.type = LAIK_AT_Reduce;
    a->fromBuf = fromBuf;
    a->toBuf = toBuf;
    a->count = count;
    a->rank = rootTask;
    a->redOp = redOp;
}

void laik_aseq_addRBufReduce(Laik_ActionSeq* as, int round,
                             int bufID, unsigned int byteOffset, unsigned int count,
                             int rootTask, Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    assert(count > 0);

    a->h.type = LAIK_AT_RBufReduce;
    a->bufID = bufID;
    a->offset = byteOffset;
    a->count = count;
    a->rank = rootTask;
    a->redOp = redOp;
}

// append group reduce action
void laik_aseq_addMapGroupReduce(Laik_ActionSeq* as, int round,
                                 int inputGroup, int outputGroup,
                                 int myInputMapNo, int myOutputMapNo,
                                 Laik_Slice* slc, Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    uint64_t count = laik_slice_size(slc);
    assert(count > 0);

    a->h.type = LAIK_AT_MapGroupReduce;
    a->inputGroup = inputGroup;
    a->outputGroup = outputGroup;
    a->fromMapNo = myInputMapNo;
    a->toMapNo = myOutputMapNo;
    a->slc = slc;
    a->redOp = redOp;
    assert(count < (1ul<<32));
    a->count = (unsigned int) count;
}


void laik_aseq_addGroupReduce(Laik_ActionSeq* as, int round,
                              int inputGroup, int outputGroup,
                              char* fromBuf, char* toBuf, unsigned int count,
                              Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    assert(count > 0);

    a->h.type = LAIK_AT_GroupReduce;
    a->inputGroup = inputGroup;
    a->outputGroup = outputGroup;
    a->fromBuf = fromBuf;
    a->toBuf = toBuf;
    a->count = count;
    a->redOp = redOp;
}

// similar to addGroupReduce
void laik_aseq_addRBufGroupReduce(Laik_ActionSeq* as, int round,
                                  int inputGroup, int outputGroup,
                                  int bufID, unsigned int byteOffset,
                                  unsigned int count,
                                  Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);
    assert(count > 0);

    a->h.type = LAIK_AT_RBufGroupReduce;
    a->inputGroup = inputGroup;
    a->outputGroup = outputGroup;
    a->bufID = bufID;
    a->offset = byteOffset;
    a->count = count;
    a->redOp = redOp;
}



void laik_aseq_addCopyToBuf(Laik_ActionSeq* as, int round,
                            Laik_CopyEntry* ce, char* toBuf, unsigned int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->h.type = LAIK_AT_CopyToBuf;
    a->ce = ce;
    a->toBuf = toBuf;
    a->count = count;
}

void laik_aseq_addCopyFromBuf(Laik_ActionSeq* as, int round,
                              Laik_CopyEntry* ce, char* fromBuf, unsigned int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->h.type = LAIK_AT_CopyFromBuf;
    a->ce = ce;
    a->fromBuf = fromBuf;
    a->count = count;
}

void laik_aseq_addCopyToRBuf(Laik_ActionSeq* as, int round,
                             Laik_CopyEntry* ce,
                             int toBufID, unsigned int toByteOffset,
                             unsigned int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->h.type = LAIK_AT_CopyToRBuf;
    a->ce = ce;
    a->bufID = toBufID;
    a->offset = toByteOffset;
    a->count = count;

}

void laik_aseq_addCopyFromRBuf(Laik_ActionSeq* as, int round,
                               Laik_CopyEntry* ce,
                               int fromBufID, unsigned int fromByteOffset,
                               unsigned int count)
{
    Laik_BackendAction* a = laik_aseq_addBAction(as, round);

    a->h.type = LAIK_AT_CopyFromRBuf;
    a->ce = ce;
    a->bufID = fromBufID;
    a->offset = fromByteOffset;
    a->count = count;
}

bool laik_action_isSend(Laik_Action* a)
{
    switch(a->type) {
    case LAIK_AT_MapSend:
    case LAIK_AT_BufSend:
    case LAIK_AT_RBufSend:
    case LAIK_AT_MapPackAndSend:
    case LAIK_AT_PackAndSend:
        return true;
    }
    return false;
}

bool laik_action_isRecv(Laik_Action* a)
{
    switch(a->type) {
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
        laik_aseq_addMapRecvAndUnpack(as, round, op->mapNo,
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
bool laik_aseq_allocBuffer(Laik_ActionSeq* as)
{
    unsigned int rCount = 0, rActions = 0;
    assert(as->bufferCount < ASEQ_BUFFER_MAX);
    assert(as->buf[as->bufferCount] == 0); // nothing allocated yet

    Laik_TransitionContext* tc = as->context[0];
    unsigned int elemsize = tc->data->elemsize;

    Laik_A_BufReserve** resAction;
    resAction = malloc(as->bufReserveCount * sizeof(Laik_A_BufReserve*));
    for(unsigned int i = 0; i < as->bufReserveCount; i++)
        resAction[i] = 0; // reservation not seen yet for ID (i-100)

    unsigned int bufSize = 0;
    Laik_Action* a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        Laik_BackendAction* ba = (Laik_BackendAction*) a;

        switch(a->type) {
        case LAIK_AT_BufReserve: {
            Laik_A_BufReserve* aa = (Laik_A_BufReserve*) a;
            // reservation already processed and allocated
            if (aa->bufID < ASEQ_BUFFER_MAX) break;

            aa->offset = bufSize;
            assert(aa->bufID >= 100);
            assert(aa->bufID < (int)(100 + as->bufReserveCount));
            resAction[aa->bufID - 100] = aa;
            aa->bufID = as->bufferCount; // mark as processed
            bufSize += aa->size;
            rCount++;
            break;
        }

        case LAIK_AT_RBufCopy:
        case LAIK_AT_RBufLocalReduce:
        case LAIK_AT_RBufSend:
        case LAIK_AT_RBufRecv:
        case LAIK_AT_RBufReduce:
        case LAIK_AT_PackToRBuf:
        case LAIK_AT_UnpackFromRBuf:
        case LAIK_AT_MapPackToRBuf:
        case LAIK_AT_MapUnpackFromRBuf:
        case LAIK_AT_CopyFromRBuf:
        case LAIK_AT_CopyToRBuf:
        case LAIK_AT_RBufGroupReduce: {
            // locate bufID/offset in different actions to update them
            int* pBufID = 0;
            unsigned int* pOffset = 0;
            unsigned int count = 0;
            switch(a->type) {
            case LAIK_AT_RBufSend:
                pBufID  = &( ((Laik_A_RBufSend*) a)->bufID );
                pOffset = &( ((Laik_A_RBufSend*) a)->offset);
                count   =    ((Laik_A_RBufSend*) a)->count;
                break;
            case LAIK_AT_RBufRecv:
                pBufID  = &( ((Laik_A_RBufRecv*) a)->bufID );
                pOffset = &( ((Laik_A_RBufRecv*) a)->offset);
                count   =    ((Laik_A_RBufRecv*) a)->count;
                break;
            case LAIK_AT_RBufCopy:
            case LAIK_AT_RBufLocalReduce:
            case LAIK_AT_RBufReduce:
            case LAIK_AT_PackToRBuf:
            case LAIK_AT_UnpackFromRBuf:
            case LAIK_AT_MapPackToRBuf:
            case LAIK_AT_MapUnpackFromRBuf:
            case LAIK_AT_CopyFromRBuf:
            case LAIK_AT_CopyToRBuf:
            case LAIK_AT_RBufGroupReduce:
                pBufID  = &( ba->bufID );
                pOffset = &( ba->offset);
                count   =    ba->count;
                break;
            default: assert(0);
            }

            // action with allocated reservation
            if (*pBufID < ASEQ_BUFFER_MAX) break;

            assert(*pBufID >= 100);
            assert(*pBufID < (int)(100 + as->bufReserveCount));
            Laik_A_BufReserve* ra = resAction[*pBufID - 100];
            assert(ra != 0);
            assert(count > 0);
            assert(*pOffset + (uint64_t)(count * elemsize) <= (uint64_t) ra->size);

            *pOffset += ra->offset;
            *pBufID = as->bufferCount; // reference into allocated buffer
            rActions++;
            break;
        }

        default:
            break;
        }
    }
    assert(as->bytesUsed == (size_t) (((char*)a) - ((char*)as->action)));

    if (bufSize == 0) {
        free(resAction);
        return false;
    }

    char* buf = malloc(bufSize);
    assert(buf != 0);

    // update allocation statistics
    laik_switchstat_malloc(tc->data->stat, bufSize);

    // substitute RBuf actions, now that buffer allocation is known
    a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        Laik_BackendAction* ba = (Laik_BackendAction*) a;
        switch(a->type) {
        case LAIK_AT_BufReserve:
            // BufReserve actions processed, can be removed
            break;
        case LAIK_AT_RBufSend: {
            // replace RBufSend with BufSend action
            Laik_A_RBufSend* aa = (Laik_A_RBufSend*) a;
            laik_aseq_addBufSend(as, a->round,
                                 buf + aa->offset, aa->count, aa->to_rank);
            break;
        }
        case LAIK_AT_RBufRecv: {
            // replace RBufRecv with BufRecv action
            Laik_A_RBufRecv* aa = (Laik_A_RBufRecv*) a;
            laik_aseq_addBufRecv(as, a->round,
                                 buf + aa->offset, aa->count, aa->from_rank);
            break;
        }
        case LAIK_AT_PackToRBuf:
            // replace PackToRBuf with PackToBuf action
            laik_aseq_addPackToBuf(as, a->round,
                                   ba->map, ba->slc, buf + ba->offset);
            break;
        case LAIK_AT_UnpackFromRBuf:
            // replace UnpackFromRBuf to UnpackFromBuf
            laik_aseq_addUnpackFromBuf(as, a->round,
                                       buf + ba->offset, ba->map, ba->slc);
            break;
        case LAIK_AT_MapPackToRBuf:
            // replace MapPackToRBuf with MapPackToBuf action
            laik_aseq_addMapPackToBuf(as, a->round,
                                      ba->fromMapNo, ba->slc, buf + ba->offset);
            break;
        case LAIK_AT_MapUnpackFromRBuf:
            // replace MapUnpackFromRBuf to MapUnpackFromBuf
            laik_aseq_addMapUnpackFromBuf(as, a->round,
                                          buf + ba->offset, ba->toMapNo, ba->slc);
            break;
        case LAIK_AT_CopyFromRBuf:
            // replace CopyFromRBuf with CopyFromBuf
            laik_aseq_addCopyFromBuf(as, a->round,
                                     ba->ce, buf + ba->offset, ba->count);
            break;
        case LAIK_AT_CopyToRBuf:
            // replace CopyToRBuf with CopyToBuf
            laik_aseq_addCopyToBuf(as, a->round,
                                   ba->ce, buf + ba->offset, ba->count);
            break;
        case LAIK_AT_RBufReduce:
            // replace RBufReduce with Reduce
            laik_aseq_addReduce(as, a->round,
                                buf + ba->offset, buf + ba->offset,
                                ba->count, ba->rank, ba->redOp);
            break;
        case LAIK_AT_RBufGroupReduce:
            // replace RBufGroupReduce with GroupReduce
            laik_aseq_addGroupReduce(as, a->round,
                                     ba->inputGroup, ba->outputGroup,
                                     buf + ba->offset, buf + ba->offset,
                                     ba->count, ba->redOp);
            break;
        default:
            // pass through
            laik_aseq_add(a, as, -1);
            break;
        }
    }
    assert(as->bytesUsed == (size_t) (((char*)a) - ((char*)as->action)));

    as->bufSize[as->bufferCount] = bufSize;
    as->buf[as->bufferCount] = buf;

    if (laik_log_begin(1)) {
        laik_log_append("RBuf alloc %d: %d reservations, %d RBuf actions, %llu bytes at %p",
                        as->bufferCount, rCount, rActions, (long long unsigned) bufSize,
                        (void*) as->buf[as->bufferCount]);
        for(unsigned int i = 0; i < as->bufReserveCount; i++) {
            if (resAction[i] == 0) continue;
            laik_log_append("\n    RBuf %d (len %d) ==> off %d at %p",
                            i + 100, resAction[i]->size,
                            resAction[i]->offset,
                            (void*) (buf + resAction[i]->offset));
        }
        laik_log_flush(0);
    }

    assert(rCount == as->bufReserveCount);
    free(resAction);
    laik_aseq_activateNewActions(as);

    // start again with bufID 100 for next reservations
    as->bufReserveCount = 0;
    as->bufferCount++;

    return true;
}


// append actions to <as>
// allows to change round of added action when <round> >= 0
void laik_aseq_add(Laik_Action* a, Laik_ActionSeq* as, int round)
{
    if (a->type == LAIK_AT_Nop) return;

    if (round < 0) round = a->round;

    Laik_Action* aCopy;
    // this unsets mark
    aCopy = laik_aseq_addAction(as, a->len, a->type, round, a->tid);

    // any further action parameters to copy?
    size_t parLen = a->len - sizeof(Laik_Action);
    if (parLen > 0)
        memcpy((char*)aCopy + sizeof(Laik_Action),
               (char*)a     + sizeof(Laik_Action), parLen);
}


// just copy actions from oldAS into as
void laik_aseq_copySeq(Laik_ActionSeq* as)
{
    assert(as->newActionCount == 0);
    Laik_Action* a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        laik_aseq_add(a, as, -1);
    }
    assert( ((char*)as->action) + as->bytesUsed == ((char*)a) );

    laik_aseq_activateNewActions(as);
}


// helpers for action combining

// check that <a> is a BufSend action with same round and peer rank as <bsa>
static bool isSameBufSend(Laik_A_BufSend* bsa, Laik_Action* a)
{
    assert(bsa->h.type == LAIK_AT_BufSend);
    if (a->type != LAIK_AT_BufSend) return false;
    if (a->round != bsa->h.round) return false;
    if ( ((Laik_A_BufSend*)a)->to_rank != bsa->to_rank) return false;
    return true;
}

// check that <a> is a BufRecv action with same round and peer rank as <bra>
static bool isSameBufRecv(Laik_A_BufRecv* bra, Laik_Action* a)
{
    assert(bra->h.type == LAIK_AT_BufRecv);
    if (a->type != LAIK_AT_BufRecv) return false;
    if (a->round != bra->h.round) return false;
    if ( ((Laik_A_BufRecv*)a)->from_rank != bra->from_rank) return false;
    return true;
}

static bool isSameGroupReduce(Laik_BackendAction* ba, Laik_Action* a)
{
    assert(ba->h.type == LAIK_AT_GroupReduce);
    if (a->type != LAIK_AT_GroupReduce) return false;
    if (a->round != ba->h.round) return false;

    Laik_BackendAction* ba2 = (Laik_BackendAction*) a;
    if (ba2->inputGroup != ba->inputGroup) return false;
    if (ba2->outputGroup != ba->outputGroup) return false;
    if (ba2->redOp != ba->redOp) return false;
    return true;
}

static bool isSameReduce(Laik_BackendAction* ba, Laik_Action* a)
{
    assert(ba->h.type == LAIK_AT_Reduce);
    if (a->type != LAIK_AT_Reduce) return false;
    if (a->round != ba->h.round) return false;

    Laik_BackendAction* ba2 = (Laik_BackendAction*) a;
    if (ba2->rank != ba->rank) return false;
    if (ba2->redOp != ba->redOp) return false;
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
    // must not have new actions, we want to start a new build
    assert(as->newActionCount == 0);

    Laik_TransitionContext* tc = as->context[0];
    Laik_Data* d = tc->data;
    unsigned int elemsize = d->elemsize;
    // used for combining GroupReduce actions
    int myid = tc->transition->group->myid;

    // unmark all actions first
    // all actions will be marked on combining, to not process them twice
    Laik_Action* a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a))
        a->mark = 0;

    // first pass: how much buffer space / copy range elements is needed?
    unsigned int bufSize = 0, copyRanges = 0;
    a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        // skip already combined actions
        if (a->mark == 1) continue;

        switch(a->type) {
        case LAIK_AT_BufSend: {
            // combine all BufSend actions in same round with same target rank
            Laik_A_BufSend* bsa = (Laik_A_BufSend*) a;
            unsigned int countSum = 0;
            unsigned int actionCount = 0;
            Laik_Action* a2 = a;
            for(unsigned int j = i; j < as->actionCount; j++, a2 = nextAction(a2)) {
                if (!isSameBufSend(bsa, a2)) continue;

                a2->mark = 1;
                countSum += ((Laik_A_BufSend*)a2)->count;
                actionCount++;
            }
            if (actionCount > 1) {
                bufSize += countSum;
                copyRanges += actionCount;
            }
            break;
        }

        case LAIK_AT_BufRecv: {
            // combine all BufRecv actions in same round with same source rank
            Laik_A_BufRecv* bra = (Laik_A_BufRecv*) a;
            unsigned int countSum = 0;
            unsigned int actionCount = 0;
            Laik_Action* a2 = a;
            for(unsigned int j = i; j < as->actionCount; j++, a2 = nextAction(a2)) {
                if (!isSameBufRecv(bra, a2)) continue;

                a2->mark = 1;
                countSum += ((Laik_A_BufRecv*)a2)->count;
                actionCount++;
            }
            if (actionCount > 1) {
                bufSize += countSum;
                copyRanges += actionCount;
            }
            break;
        }

        case LAIK_AT_GroupReduce: {
            // combine all GroupReduce actions with same
            // inputGroup, outputGroup, and redOp
            Laik_BackendAction* ba = (Laik_BackendAction*) a;
            unsigned int countSum = 0;
            unsigned int actionCount = 0;
            Laik_Action* a2 = a;
            for(unsigned int j = i; j < as->actionCount; j++, a2 = nextAction(a2)) {
                if (!isSameGroupReduce(ba, a2)) continue;

                a2->mark = 1;
                countSum += ((Laik_BackendAction*)a2)->count;
                actionCount++;
            }
            if (actionCount > 1) {
                bufSize += countSum;
                if (laik_trans_isInGroup(tc->transition, ba->inputGroup, myid))
                    copyRanges += actionCount;
                if (laik_trans_isInGroup(tc->transition, ba->outputGroup, myid))
                    copyRanges += actionCount;
            }
            break;
        }

        case LAIK_AT_Reduce: {
            // combine all reduce actions with same root and redOp
            Laik_BackendAction* ba = (Laik_BackendAction*) a;
            unsigned int countSum = 0;
            unsigned int actionCount = 0;
            Laik_Action* a2 = a;
            for(unsigned int j = i; j < as->actionCount; j++, a2 = nextAction(a2)) {
                if (!isSameReduce(ba, a2)) continue;

                a2->mark = 1;
                countSum += ((Laik_BackendAction*)a2)->count;
                actionCount++;
            }
            if (actionCount > 1) {
                bufSize += countSum;
                // always providing input, copy input ranges
                copyRanges += actionCount;
                // if I want result, we can reuse the input ranges
                if ((ba->rank == myid) || (ba->rank == -1))
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
    as->ceRanges += copyRanges;

    int bufID = laik_aseq_addBufReserve(as, bufSize * elemsize, -1);

    laik_log(1, "Reservation for combined actions: length %d x %d, ranges %d",
             bufSize, elemsize, copyRanges);

    // unmark all actions: restart for finding same type of actions
    a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a))
        a->mark = 0;

    // second pass: add merged actions
    unsigned int bufOff = 0;
    unsigned int rangeOff = 0;

    a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        // skip already processed actions
        if (a->mark == 1) continue;

        switch(a->type) {
        case LAIK_AT_BufSend: {
            Laik_A_BufSend* bsa = (Laik_A_BufSend*) a;
            unsigned int countSum = 0;
            unsigned int actionCount = 0;
            Laik_Action* a2 = a;
            for(unsigned int j = i; j < as->actionCount; j++, a2 = nextAction(a2)) {
                if (!isSameBufSend(bsa, a2)) continue;

                a2->mark = 1;
                countSum += ((Laik_A_BufSend*)a2)->count;
                actionCount++;
            }
            if (actionCount > 1) {
                //laik_log(1,"Send Seq %d - %d, rangeOff %d, bufOff %d, count %d",
                //         i, j, rangeOff, bufOff, count);
                laik_aseq_addCopyToRBuf(as, 3 * a->round,
                                        ce + rangeOff,
                                        bufID, 0,
                                        actionCount);
                laik_aseq_addRBufSend(as, 3 * a->round + 1,
                                      bufID, bufOff * elemsize,
                                      countSum, bsa->to_rank);
                unsigned int oldRangeOff = rangeOff;
                Laik_Action* a2 = a;
                for(unsigned int k = i; k < as->actionCount; k++, a2 = nextAction(a2)) {
                    if (!isSameBufSend(bsa, a2)) continue;

                    Laik_A_BufSend* bsa2 = (Laik_A_BufSend*) a2;
                    assert(rangeOff < copyRanges);
                    ce[rangeOff].ptr = bsa2->buf;
                    ce[rangeOff].bytes = bsa2->count * elemsize;
                    ce[rangeOff].offset = bufOff * elemsize;
                    bufOff += bsa2->count;
                    rangeOff++;
                }
                assert(oldRangeOff + actionCount == rangeOff);
            }
            else
                laik_aseq_addBufSend(as, 3 * a->round + 1,
                                     bsa->buf, bsa->count, bsa->to_rank);
            break;
        }

        case LAIK_AT_BufRecv: {
            Laik_A_BufRecv* bra = (Laik_A_BufRecv*) a;
            unsigned int countSum = 0;
            unsigned int actionCount = 0;
            Laik_Action* a2 = a;
            for(unsigned int j = i; j < as->actionCount; j++, a2 = nextAction(a2)) {
                if (!isSameBufRecv(bra, a2)) continue;

                a2->mark = 1;
                countSum += ((Laik_A_BufRecv*)a2)->count;
                actionCount++;
            }
            if (actionCount > 1) {
                laik_aseq_addRBufRecv(as, 3 * a->round + 1,
                                      bufID, bufOff * elemsize,
                                      countSum, bra->from_rank);
                laik_aseq_addCopyFromRBuf(as, 3 * a->round + 2,
                                          ce + rangeOff,
                                          bufID, 0,
                                          actionCount);
                unsigned int oldRangeOff = rangeOff;
                Laik_Action* a2 = a;
                for(unsigned int k = i; k < as->actionCount; k++, a2 = nextAction(a2)) {
                    if (!isSameBufRecv(bra, a2)) continue;

                    Laik_A_BufRecv* bra2 = (Laik_A_BufRecv*) a2;
                    assert(rangeOff < copyRanges);
                    ce[rangeOff].ptr = bra2->buf;
                    ce[rangeOff].bytes = bra2->count * elemsize;
                    ce[rangeOff].offset = bufOff * elemsize;
                    bufOff += bra2->count;
                    rangeOff++;
                }
                assert(oldRangeOff + actionCount == rangeOff);
            }
            else
                laik_aseq_addBufRecv(as, 3 * a->round + 1,
                                     bra->buf, bra->count, bra->from_rank);
            break;
        }

        case LAIK_AT_GroupReduce: {
            Laik_BackendAction* ba = (Laik_BackendAction*) a;
            unsigned int countSum = 0;
            unsigned int actionCount = 0;
            Laik_Action* a2 = a;
            for(unsigned int j = i; j < as->actionCount; j++, a2 = nextAction(a2)) {
                if (!isSameGroupReduce(ba, a2)) continue;

                a2->mark = 1;
                countSum += ((Laik_BackendAction*)a2)->count;
                actionCount++;
            }
            if (actionCount > 1) {
                // temporary buffer used as input and output for reduce
                unsigned int startBufOff = bufOff;

                // if I provide input: copy pieces into temporary buffer
                if (laik_trans_isInGroup(tc->transition, ba->inputGroup, myid)) {
                    laik_aseq_addCopyToRBuf(as, 3 * a->round,
                                            ce + rangeOff,
                                            bufID, 0,
                                            actionCount);
                    // ranges for input pieces
                    unsigned int oldRangeOff = rangeOff;
                    Laik_Action* a2 = a;
                    for(unsigned int k = i; k < as->actionCount; k++, a2 = nextAction(a2)) {
                        if (!isSameGroupReduce(ba, a2)) continue;

                        Laik_BackendAction* ba2 = (Laik_BackendAction*) a2;
                        assert(rangeOff < copyRanges);
                        ce[rangeOff].ptr = ba2->fromBuf;
                        ce[rangeOff].bytes = ba2->count * elemsize;
                        ce[rangeOff].offset = bufOff * elemsize;
                        bufOff += ba2->count;
                        rangeOff++;
                    }
                    assert(oldRangeOff + actionCount == rangeOff);
                    assert(startBufOff + countSum == bufOff);
                }

                // use temporary buffer for both input and output
                laik_aseq_addRBufGroupReduce(as, 3 * a->round + 1,
                                             ba->inputGroup, ba->outputGroup,
                                             bufID, startBufOff * elemsize,
                                             countSum, ba->redOp);

                // if I want output: copy pieces from temporary buffer
                if (laik_trans_isInGroup(tc->transition, ba->outputGroup, myid)) {
                    laik_aseq_addCopyFromRBuf(as, 3 * a->round + 2,
                                              ce + rangeOff,
                                              bufID, 0,
                                              actionCount);
                    bufOff = startBufOff;
                    unsigned int oldRangeOff = rangeOff;
                    Laik_Action* a2 = a;
                    for(unsigned int k = i; k < as->actionCount; k++, a2 = nextAction(a2)) {
                        if (!isSameGroupReduce(ba, a2)) continue;

                        Laik_BackendAction* ba2 = (Laik_BackendAction*) a2;
                        assert(rangeOff < copyRanges);
                        ce[rangeOff].ptr = ba2->toBuf;
                        ce[rangeOff].bytes = ba2->count * elemsize;
                        ce[rangeOff].offset = bufOff * elemsize;
                        bufOff += ba2->count;
                        rangeOff++;
                    }
                    assert(oldRangeOff + actionCount == rangeOff);
                    assert(startBufOff + countSum == bufOff);
                }
                bufOff = startBufOff + countSum;
            }
            else
                laik_aseq_addGroupReduce(as, 3 * a->round + 1,
                                         ba->inputGroup, ba->outputGroup,
                                         ba->fromBuf, ba->toBuf,
                                         ba->count, ba->redOp);
            break;
        }

        case LAIK_AT_Reduce: {
            Laik_BackendAction* ba = (Laik_BackendAction*) a;
            unsigned int countSum = 0;
            unsigned int actionCount = 0;
            Laik_Action* a2 = a;
            for(unsigned int j = i; j < as->actionCount; j++, a2 = nextAction(a2)) {
                if (!isSameReduce(ba, a2)) continue;

                a2->mark = 1;
                countSum += ((Laik_BackendAction*)a2)->count;
                actionCount++;
            }
            if (actionCount > 1) {
                // temporary buffer used as input and output for reduce
                unsigned int startBufOff = bufOff;

                // copy input pieces into temporary buffer
                laik_aseq_addCopyToRBuf(as, 3 * a->round,
                                        ce + rangeOff,
                                        bufID, 0,
                                        actionCount);
                // ranges for input pieces
                unsigned int oldRangeOff = rangeOff;
                Laik_Action* a2 = a;
                for(unsigned int k = i; k < as->actionCount; k++, a2 = nextAction(a2)) {
                    if (!isSameReduce(ba, a2)) continue;

                    Laik_BackendAction* ba2 = (Laik_BackendAction*) a2;
                    assert(rangeOff < copyRanges);
                    ce[rangeOff].ptr = ba2->fromBuf;
                    ce[rangeOff].bytes = ba2->count * elemsize;
                    ce[rangeOff].offset = bufOff * elemsize;
                    bufOff += ba2->count;
                    rangeOff++;
                }
                assert(oldRangeOff + actionCount == rangeOff);
                assert(startBufOff + countSum == bufOff);

                // use temporary buffer for both input and output
                laik_aseq_addRBufReduce(as, 3 * a->round + 1,
                                           bufID, startBufOff * elemsize,
                                           countSum, ba->rank, ba->redOp);

                // if I want result, copy output ranges
                if ((ba->rank == myid) || (ba->rank == -1)) {
                    // collect output ranges: we cannot reuse copy ranges from
                    // input pieces because of potentially other output buffers
                    laik_aseq_addCopyFromRBuf(as, 3 * a->round + 2,
                                              ce + rangeOff,
                                              bufID, 0,
                                              actionCount);
                    bufOff = startBufOff;
                    unsigned int oldRangeOff = rangeOff;
                    Laik_Action* a2 = a;
                    for(unsigned int k = i; k < as->actionCount; k++, a2 = nextAction(a2)) {
                        if (!isSameReduce(ba, a2)) continue;

                        Laik_BackendAction* ba2 = (Laik_BackendAction*) a2;
                        assert(rangeOff < copyRanges);
                        ce[rangeOff].ptr = ba2->toBuf;
                        ce[rangeOff].bytes = ba2->count * elemsize;
                        ce[rangeOff].offset = bufOff * elemsize;
                        bufOff += ba2->count;
                        rangeOff++;
                    }
                    assert(oldRangeOff + actionCount == rangeOff);
                    assert(startBufOff + countSum == bufOff);
                }
                bufOff = startBufOff + countSum;
            }
            else
                laik_aseq_addReduce(as, 3 * a->round + 1,
                                    ba->fromBuf, ba->toBuf,
                                    ba->count, ba->rank, ba->redOp);
            break;
        }

        default:
            // pass through
            laik_aseq_add(a, as, 3 * a->round + 1);
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
void addResorted(unsigned int count, Laik_Action** order, Laik_ActionSeq* as)
{
    assert(count > 0);

    // add actions in new order to as2, compress rounds
    int round;
    int oldRound = order[0]->round;
    int newRound = 0;
    for(unsigned int i = 0; i < count; i++) {
        round = order[i]->round;
        assert(round >= oldRound); // must be sorted
        if (round > oldRound) {
            oldRound = round;
            newRound++;
        }
        laik_aseq_add(order[i], as, newRound);
    }
}

static
int getActionPeer(Laik_Action* a)
{
    switch(a->type) {
    case LAIK_AT_RBufSend: return ((Laik_A_RBufSend*)a)->to_rank;
    case LAIK_AT_RBufRecv: return ((Laik_A_RBufRecv*)a)->from_rank;
    case LAIK_AT_BufSend:  return ((Laik_A_BufSend*)a)->to_rank;
    case LAIK_AT_BufRecv:  return ((Laik_A_BufRecv*)a)->from_rank;
    case LAIK_AT_MapPackAndSend:
        return ((Laik_A_MapPackAndSend*)a)->to_rank;
    case LAIK_AT_MapRecvAndUnpack:
        return ((Laik_A_MapRecvAndUnpack*)a)->from_rank;
    case LAIK_AT_MapSend:
    case LAIK_AT_MapRecv:
    case LAIK_AT_PackAndSend:
    case LAIK_AT_RecvAndUnpack:
        return ((Laik_BackendAction*)a)->rank;
    default: assert(0); // only should be called for send/recv actions
    }
    return 0;
}

// used by compare functions, set directly before sort
static int myid4cmp;

static
int cmp2phase(const void* aptr1, const void* aptr2)
{
    Laik_Action* a1 = *((Laik_Action* const *) aptr1);
    Laik_Action* a2 = *((Laik_Action* const *) aptr2);

    if (a1->round != a2->round)
        return a1->round - a2->round;

    bool a1isSend = laik_action_isSend(a1);
    bool a2isSend = laik_action_isSend(a2);
    bool a1isRecv = laik_action_isRecv(a1);
    bool a2isRecv = laik_action_isRecv(a2);
    int a1peer = (a1isSend || a1isRecv) ? getActionPeer(a1) : 0;
    int a2peer = (a2isSend || a2isRecv) ? getActionPeer(a2) : 0;

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
        return (int) (a1 - a2);
    }

    // both are neither send/recv actions: keep same order
    // we can compare pointers to actions (as they are not sorted directly!)
    return (int) (a1 - a2);
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

    Laik_Action** order = malloc(as->actionCount * sizeof(void*));
    Laik_Action* a = as->action;
    for(unsigned int i=0; i < as->actionCount; i++, a = nextAction(a))
        order[i] = a;

    Laik_TransitionContext* tc = as->context[0];
    myid4cmp = tc->transition->group->myid;
    qsort(order, as->actionCount, sizeof(void*), cmp2phase);

    // check if something changed
    bool changed = false;
    a = as->action;
    for(unsigned int i=0; i < as->actionCount; i++, a = nextAction(a)) {
        if (order[i] == a) continue;
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
    Laik_Action* a1 = *((Laik_Action* const *) aptr1);
    Laik_Action* a2 = *((Laik_Action* const *) aptr2);

    if (a1->round != a2->round)
        return a1->round - a2->round;

    bool a1isSend = laik_action_isSend(a1);
    bool a2isSend = laik_action_isSend(a2);
    bool a1isRecv = laik_action_isRecv(a1);
    bool a2isRecv = laik_action_isRecv(a2);
    int a1peer = getActionPeer(a1);
    int a2peer = getActionPeer(a2);

    // phase number is number of lower digits equal to my rank
    int a1phase = 0, a2phase = 0, mask = 0;
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
    return (int) (a1 - a2);
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

    Laik_Action** order = malloc(as->actionCount * sizeof(void*));
    Laik_Action* a = as->action;
    for(unsigned int i=0; i < as->actionCount; i++, a = nextAction(a))
        order[i] = a;

    Laik_TransitionContext* tc = as->context[0];
    myid4cmp = tc->transition->group->myid;
    qsort(order, as->actionCount, sizeof(void*), cmp_rankdigits);

    // check if something changed
    bool changed = false;
    a = as->action;
    for(unsigned int i=0; i < as->actionCount; i++, a = nextAction(a)) {
        if (order[i] == a) continue;
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
    Laik_Action* a1 = *((Laik_Action* const *) aptr1);
    Laik_Action* a2 = *((Laik_Action* const *) aptr2);

    if (a1->round != a2->round)
        return a1->round - a2->round;

    // otherwise, keep original order
    // we can compare pointers to actions (as they are not sorted directly!)
    return (int) (a1 - a2);
}

// sort actions according to their rounds, and compress rounds
bool laik_aseq_sort_rounds(Laik_ActionSeq* as)
{
    if (as->actionCount == 0) return false;

    // must not have new actions, we want to start a new build
    assert(as->newActionCount == 0);

    Laik_Action** order = malloc(as->actionCount * sizeof(void*));
    Laik_Action* a = as->action;
    for(unsigned int i=0; i < as->actionCount; i++, a = nextAction(a))
        order[i] = a;

    qsort(order, as->actionCount, sizeof(void*), cmp_rounds);

    // check if something changed
    bool changed = false;
    a = as->action;
    for(unsigned int i=0; i < as->actionCount; i++, a = nextAction(a)) {
        if (order[i] == a) continue;
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
    unsigned int count;

    // must not have new actions, we want to start a new build
    assert(as->newActionCount == 0);

    Laik_TransitionContext* tc = as->context[0];
    unsigned int elemsize = tc->data->elemsize;
    int myid = tc->transition->group->myid;

    Laik_Action* a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        Laik_BackendAction* ba = (Laik_BackendAction*) a;
        bool handled = false;

        switch(a->type) {
        case LAIK_AT_MapPackAndSend: {
            Laik_A_MapPackAndSend* aa = (Laik_A_MapPackAndSend*) a;

            if (tc->fromList)
                assert(aa->fromMapNo < tc->fromList->count);
            fromMap = tc->fromList ? &(tc->fromList->map[aa->fromMapNo]) : 0;

            if (fromMap && (aa->slc->space->dims == 1)) {
                // mapping known and 1d: can use direct send/recv

                // FIXME: this assumes lexicographical layout
                from = aa->slc->from.i[0] - fromMap->requiredSlice.from.i[0];
                to   = aa->slc->to.i[0] - fromMap->requiredSlice.from.i[0];
                assert(from >= 0);
                assert(to > from);
                count = (unsigned int)(to - from);

                // replace with different action depending on map allocation done
                if (fromMap->base)
                    laik_aseq_addBufSend(as, 3 * a->round + 1,
                                         fromMap->base + from * elemsize,
                                         count, aa->to_rank);
                else {
                    assert(from * elemsize < (1l<<32));
                    unsigned int offset = (unsigned int) from * elemsize;
                    laik_aseq_addMapSend(as, 3 * a->round + 1,
                                         aa->fromMapNo, offset,
                                         count, aa->to_rank);
                }
            }
            else {
                // split off packing and sending, using a buffer of required size
                int bufID = laik_aseq_addBufReserve(as, aa->count * elemsize, -1);
                if (fromMap)
                    laik_aseq_addPackToRBuf(as, 3 * a->round,
                                            fromMap, aa->slc, bufID, 0);
                else
                    laik_aseq_addMapPackToRBuf(as, 3 * a->round,
                                               aa->fromMapNo, aa->slc, bufID, 0);

                laik_aseq_addRBufSend(as, 3 * a->round + 1,
                                      bufID, 0, aa->count, aa->to_rank);
            }
            handled = true;
            break;
        }

        case LAIK_AT_MapRecvAndUnpack: {
            Laik_A_MapRecvAndUnpack* aa = (Laik_A_MapRecvAndUnpack*) a;

            if (tc->toList)
                assert(aa->toMapNo < tc->toList->count);
            toMap = tc->toList ? &(tc->toList->map[aa->toMapNo]) : 0;

            if (toMap && (aa->slc->space->dims == 1)) {
                // mapping known and 1d: can use direct send/recv

                // FIXME: this assumes lexicographical layout
                from = aa->slc->from.i[0] - toMap->requiredSlice.from.i[0];
                to   = aa->slc->to.i[0] - toMap->requiredSlice.from.i[0];
                assert(from >= 0);
                assert(to > from);
                count = (unsigned int)(to - from);

                // replace with different action depending on map allocation done
                if (toMap->base)
                    laik_aseq_addBufRecv(as, 3 * a->round + 1,
                                         toMap->base + from * elemsize,
                                         count, aa->from_rank);
                else {
                    assert(from * elemsize < (1l<<32));
                    unsigned int offset = (unsigned int) from * elemsize;
                    laik_aseq_addMapRecv(as, 3 * a->round + 1,
                                         aa->toMapNo, offset,
                                         count, aa->from_rank);
                }
            }
            else {
                // split off receiving and unpacking, using buffer of required size
                int bufID = laik_aseq_addBufReserve(as, aa->count * elemsize, -1);
                laik_aseq_addRBufRecv(as, 3 * a->round + 1,
                                      bufID, 0, aa->count, aa->from_rank);
                if (toMap)
                    laik_aseq_addUnpackFromRBuf(as, 3 * a->round + 2,
                                                bufID, 0, toMap, aa->slc);
                else
                    laik_aseq_addMapUnpackFromRBuf(as, 3 * a->round + 2,
                                                   bufID, 0, aa->toMapNo, aa->slc);
            }
            handled = true;
            break;
        }

        case LAIK_AT_MapGroupReduce:

            // TODO: for >1 dims, use pack/unpack with buffer
            if (ba->slc->space->dims == 1) {
                char *fromBase, *toBase;

                // if current task is input, fromBase should be allocated
                if (laik_trans_isInGroup(tc->transition, ba->inputGroup, myid)) {
                    assert(tc->fromList);
                    assert(ba->fromMapNo < tc->fromList->count);
                    fromMap = &(tc->fromList->map[ba->fromMapNo]);
                    fromBase = fromMap ? fromMap->base : 0;
                    assert(fromBase != 0);
                }
                else {
                    fromBase = 0;
                    fromMap = 0;
                }

                // if current task is receiver, toBase should be allocated
                if (laik_trans_isInGroup(tc->transition, ba->outputGroup, myid)) {
                    assert(tc->toList);
                    assert(ba->toMapNo < tc->toList->count);
                    toMap = &(tc->toList->map[ba->toMapNo]);
                    toBase = toMap ? toMap->base : 0;
                    assert(toBase != 0);
                }
                else {
                    toBase = 0; // no interest in receiving anything
                    toMap = 0;
                }

                // FIXME: this assumes lexicographical layout
                from = ba->slc->from.i[0];
                to   = ba->slc->to.i[0];
                assert(to > from);
                count = (unsigned int)(to - from);

                if (fromBase) {
                    assert(from >= fromMap->requiredSlice.from.i[0]);
                    fromBase += (from - fromMap->requiredSlice.from.i[0]) * elemsize;
                }
                if (toBase) {
                    assert(from >= toMap->requiredSlice.from.i[0]);
                    toBase += (from - toMap->requiredSlice.from.i[0]) * elemsize;
                }

                laik_aseq_addGroupReduce(as, 3 * a->round + 1,
                                         ba->inputGroup, ba->outputGroup,
                                         fromBase, toBase, count, ba->redOp);
                handled = true;
            }
            break;

        default: break;
        }

        if (!handled)
            laik_aseq_add(a, as, 3 * a->round + 1);
        else
            changed = true;
    }
    assert( ((char*)as->action) + as->bytesUsed == ((char*)a) );

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
                                Laik_TransitionContext* tc, Laik_BackendAction* ba)
{
    assert(ba->h.type == LAIK_AT_GroupReduce);
    Laik_Transition* t = tc->transition;
    Laik_Data* data = tc->data;

    // do the manual reduction on smallest rank of output group
    int reduceTask = laik_trans_taskInGroup(t, ba->outputGroup, 0);
    int myid = t->group->myid;

    if (myid != reduceTask) {
        // not the reduce task: eventually send input and recv result

        if (laik_trans_isInGroup(t, ba->inputGroup, myid)) {
            // send action in round 0
            laik_aseq_addBufSend(as, 3 * ba->h.round,
                                 ba->fromBuf, ba->count, reduceTask);
        }

        if (laik_trans_isInGroup(t, ba->outputGroup, myid)) {
            // recv action only in round 2
            laik_aseq_addBufRecv(as, 3 * ba->h.round + 2,
                                 ba->toBuf, ba->count, reduceTask);
        }

        return;
    }

    // we are the reduce task

    int inCount = laik_trans_groupCount(t, ba->inputGroup);
    unsigned int byteCount = ba->count * data->elemsize;

    bool inputFromMe = laik_trans_isInGroup(t, ba->inputGroup, myid);
    assert(inCount >= 0);
    unsigned int inCountWithoutMe = (unsigned int) inCount;
    if (inputFromMe) {
        assert(inCountWithoutMe > 0);
        inCountWithoutMe--;
    }

    // buffer for all partial input values
    unsigned int bufSize = inCountWithoutMe * byteCount;
    int bufID = -1;
    if (bufSize > 0)
        bufID = laik_aseq_addBufReserve(as, bufSize, -1);

    // collect values from tasks in input group
    unsigned int bufOff[32], off = 0;
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
        int inTask = laik_trans_taskInGroup(t, ba->inputGroup, i);
        if (inTask == myid) continue;

        laik_aseq_addRBufRecv(as, 3 * ba->h.round,
                              bufID, off, ba->count, inTask);
        bufOff[ii++] = off;
        off += byteCount;
    }
    assert(ii == inCount);
    assert(off == bufSize);

    if (inCount == 0) {
        // no input: add init action for neutral element of reduction
        laik_aseq_addBufInit(as, 3 * ba->h.round + 1,
                             data->type, ba->redOp, ba->toBuf, ba->count);
    }
    else {
        // move first input to a->toBuf, and then reduce on that
        if (inputFromMe) {
            if (ba->fromBuf != ba->toBuf) {
                // if my input is not already at a->toBuf, copy it
                laik_aseq_addBufCopy(as, 3 * ba->h.round + 1,
                                     ba->fromBuf, ba ->toBuf, ba->count);
            }
        }
        else {
            // copy first input to a->toBuf
            laik_aseq_addRBufCopy(as,  3 * ba->h.round + 1,
                                  bufID, bufOff[0], ba->toBuf, ba->count);
        }

        // do reduction with other inputs
        for(int t = 1; t < inCount; t++)
            laik_aseq_addRBufLocalReduce(as, 3 * ba->h.round + 1,
                                         data->type, ba->redOp,
                                         bufID, bufOff[t],
                                         ba->toBuf, ba->count);
    }

    // send result to tasks in output group
    int outCount = laik_trans_groupCount(t, ba->outputGroup);
    for(int i = 0; i< outCount; i++) {
        int outTask = laik_trans_taskInGroup(t, ba->outputGroup, i);
        if (outTask == myid) {
            // that's myself: nothing to do
            continue;
        }

        laik_aseq_addBufSend(as,  3 * ba->h.round + 2,
                             ba->toBuf, ba->count, outTask);
    }
}

// add actions for 2-step manual reduction for a group-reduce action
// intermixed with 3-step reduction, thus need to spread rounds by 3
// round 0: send/recv, round 1: reduction
static
void laik_aseq_addReduce2Rounds(Laik_ActionSeq* as,
                                Laik_TransitionContext* tc, Laik_BackendAction* ba)
{
    assert(ba->h.type == LAIK_AT_GroupReduce);
    Laik_Transition* t = tc->transition;
    Laik_Data* data = tc->data;

    // everybody sends his input to all others, everybody does reduction
    int myid = t->group->myid;

    bool inputFromMe = laik_trans_isInGroup(t, ba->inputGroup, myid);
    if (inputFromMe) {
        // send my input to all tasks in output group
        int outCount = laik_trans_groupCount(t, ba->outputGroup);
        for(int i = 0; i< outCount; i++) {
            int outTask = laik_trans_taskInGroup(t, ba->outputGroup, i);
            if (outTask == myid) {
                // that's myself: nothing to do
                continue;
            }

            laik_aseq_addBufSend(as,  3 * ba->h.round,
                                 ba->fromBuf, ba->count, outTask);
        }
    }

    if (!laik_trans_isInGroup(t, ba->outputGroup, myid)) return;

    // I am interested in result, process inputs from others

    int inCount = laik_trans_groupCount(t, ba->inputGroup);
    unsigned int byteCount = ba->count * data->elemsize;

    // buffer for all partial input values
    assert(inCount >= 0);
    unsigned int inCountWithoutMe = (unsigned int) inCount;
    if (inputFromMe) {
        assert(inCountWithoutMe > 0);
        inCountWithoutMe--;
    }
    unsigned int bufSize = inCountWithoutMe * byteCount;

    int bufID = -1;
    if (bufSize > 0)
        bufID = laik_aseq_addBufReserve(as, bufSize, -1);

    // collect values from tasks in input group
    unsigned int bufOff[32], off = 0;
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
        int inTask = laik_trans_taskInGroup(t, ba->inputGroup, i);
        if (inTask == myid) continue;

        laik_aseq_addRBufRecv(as, 3 * ba->h.round,
                              bufID, off, ba->count, inTask);
        bufOff[ii++] = off;
        off += byteCount;
    }
    assert(ii == inCount);
    assert(off == bufSize);

    if (inCount == 0) {
        // no input: add init action for neutral element of reduction
        laik_aseq_addBufInit(as, 3 * ba->h.round + 1,
                             data->type, ba->redOp, ba->toBuf, ba->count);
    }
    else {
        // move first input to a->toBuf, and then reduce on that
        if (inputFromMe) {
            if (ba->fromBuf != ba->toBuf) {
                // if my input is not already at a->toBuf, copy it
                laik_aseq_addBufCopy(as, 3 * ba->h.round +1,
                                     ba->fromBuf, ba ->toBuf, ba->count);
            }
        }
        else {
            // copy first input to a->toBuf
            laik_aseq_addRBufCopy(as, 3 * ba->h.round + 1,
                                  bufID, bufOff[0], ba->toBuf, ba->count);
        }

        // do reduction with other inputs
        for(int t = 1; t < inCount; t++)
            laik_aseq_addRBufLocalReduce(as, 3 * ba->h.round +1,
                                         data->type, ba->redOp,
                                         bufID, bufOff[t],
                                         ba->toBuf, ba->count);
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

    Laik_Action* a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        if (a->type == LAIK_AT_GroupReduce) {
            reduceFound = true;
            break;
        }
    }
    if (!reduceFound)
        return false;

    a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        Laik_BackendAction* ba = (Laik_BackendAction*) a;

        switch(a->type) {
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
            laik_aseq_add(a, as, 3 * a->round + 1);
            break;
        }
    }
    assert( ((char*)as->action) + as->bytesUsed == ((char*)a) );

    laik_aseq_activateNewActions(as);
    return true;
}

// replace group reduction actions with all-reduction actions if possible
bool laik_aseq_replaceWithAllReduce(Laik_ActionSeq* as)
{
    bool changed = false;
    assert(as->newActionCount == 0);

    Laik_TransitionContext* tc = as->context[0];
    Laik_Transition* t = tc->transition;

    Laik_Action* a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        Laik_BackendAction* ba = (Laik_BackendAction*) a;

        switch(a->type) {
        // TODO: LAIK_AT_MapGroupReduce
        case LAIK_AT_GroupReduce:
            if (ba->inputGroup == -1) {
                if (ba->outputGroup == -1) {
                    laik_aseq_addReduce(as, a->round, ba->fromBuf, ba->toBuf,
                                        ba->count, -1, ba->redOp);
                    changed = true;
                    continue;
                }
                else if (laik_trans_groupCount(t, ba->outputGroup) == 1) {
                    int root = laik_trans_taskInGroup(t, ba->outputGroup, 0);
                    laik_aseq_addReduce(as, a->round, ba->fromBuf, ba->toBuf,
                                        ba->count, root, ba->redOp);
                    changed = true;
                    continue;
                }
            }
            laik_aseq_add(a, as, -1);
            break;

        default:
            laik_aseq_add(a, as, -1);
            break;
        }
    }
    assert( ((char*)as->action) + as->bytesUsed == ((char*)a) );

    if (changed)
        laik_aseq_activateNewActions(as);
    else
        laik_aseq_discardNewActions(as);

    return changed;
}

// replace transition exec actions with equivalent reduce/send/recv actions
bool laik_aseq_splitTransitionExecs(Laik_ActionSeq* as)
{
    // must not have new actions, we want to start a new build
    assert(as->newActionCount == 0);

    Laik_TransitionContext* tc = as->context[0];
    bool found = false;
    Laik_Action* a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        if (a->type == LAIK_AT_TExec) {
            found = true;
            break;
        }
    }
    if (!found)
        return false;

    a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        switch(a->type) {
        case LAIK_AT_TExec:
            assert(a->tid == 0);
            laik_aseq_addReds(as, a->round, tc->data, tc->transition);
            laik_aseq_addSends(as, a->round, tc->data, tc->transition);
            laik_aseq_addRecvs(as, a->round, tc->data, tc->transition);
            break;

        default:
            laik_aseq_add(a, as, -1);
            break;
        }
    }
    assert( ((char*)as->action) + as->bytesUsed == ((char*)a) );

    laik_aseq_activateNewActions(as);
    return true;
}

// calculate stats of one run of the action sequence
// (if the action seq has backend-specific actions, a corresponding function in the
//  backend needs to be called in addition)
// must be called before laik_switchstat_addASeq() for correct stats
// return number of not handled actions
int laik_aseq_calc_stats(Laik_ActionSeq* as)
{
    Laik_CopyEntry* ce;
    int not_processed = 0;
    unsigned int count = 0;

    as->msgSendCount = 0;
    as->msgRecvCount = 0;
    as->msgReduceCount = 0;
    as->msgAsyncSendCount = 0;
    as->msgAsyncRecvCount = 0;
    as->elemSendCount = 0;
    as->elemRecvCount = 0;
    as->elemReduceCount = 0;
    as->byteSendCount = 0;
    as->byteRecvCount = 0;
    as->byteReduceCount = 0;
    as->initOpCount = 0;
    as->reduceOpCount = 0;
    as->byteBufCopyCount = 0;

    // TODO: we only allow 1 transition at the moment
    Laik_TransitionContext* tc = as->context[0];
    int current_tid = 0;
    as->transitionCount = 1;

    Laik_Action* a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        assert(a->tid == current_tid); // TODO: only assumes actions from one transition

        switch(a->type) {
        case LAIK_AT_TExec:
            break;

        // these actions are not expected to be found
        case LAIK_AT_BufReserve:
            assert(0);
            break;

        case LAIK_AT_MapSend:
        case LAIK_AT_BufSend:
        case LAIK_AT_RBufSend:
        case LAIK_AT_PackAndSend:
        case LAIK_AT_MapPackAndSend:
            switch(a->type) {
            case LAIK_AT_RBufSend:    count = ((Laik_A_RBufSend*)a)->count; break;
            case LAIK_AT_MapSend:
            case LAIK_AT_PackAndSend: count = ((Laik_BackendAction*)a)->count; break;
            case LAIK_AT_BufSend:     count = ((Laik_A_BufSend*)a)->count; break;
            case LAIK_AT_MapPackAndSend:
                count = ((Laik_A_MapPackAndSend*)a)->count; break;
            }
            as->msgSendCount++;
            as->elemSendCount += count;
            as->byteSendCount += count * tc->data->elemsize;

            switch(a->type) {
            case LAIK_AT_PackAndSend:
            case LAIK_AT_MapPackAndSend:
                as->byteBufCopyCount += count * tc->data->elemsize;
                break;
            }
            break;

        case LAIK_AT_MapRecv:
        case LAIK_AT_BufRecv:
        case LAIK_AT_RBufRecv:
        case LAIK_AT_RecvAndUnpack:
        case LAIK_AT_MapRecvAndUnpack:
            switch(a->type) {
            case LAIK_AT_RBufRecv:      count = ((Laik_A_RBufRecv*)a)->count; break;
            case LAIK_AT_MapRecv:
            case LAIK_AT_RecvAndUnpack: count = ((Laik_BackendAction*)a)->count; break;
            case LAIK_AT_BufRecv:       count = ((Laik_A_BufRecv*)a)->count; break;
            case LAIK_AT_MapRecvAndUnpack:
                count = ((Laik_A_MapRecvAndUnpack*)a)->count; break;
            }
            as->msgRecvCount++;
            as->elemRecvCount += count;
            as->byteRecvCount += count * tc->data->elemsize;

            switch(a->type) {
            case LAIK_AT_RecvAndUnpack:
            case LAIK_AT_MapRecvAndUnpack:
                as->byteBufCopyCount += count * tc->data->elemsize;
                break;
            }
            break;

        case LAIK_AT_Reduce:
        case LAIK_AT_RBufReduce:
        case LAIK_AT_MapGroupReduce:
        case LAIK_AT_GroupReduce:
        case LAIK_AT_RBufGroupReduce:
            count = ((Laik_BackendAction*)a)->count;
            as->msgReduceCount++;
            as->elemReduceCount += count;
            as->byteReduceCount += count * tc->data->elemsize;
            break;

        case LAIK_AT_RBufLocalReduce:
            as->reduceOpCount += ((Laik_BackendAction*)a)->count;
            break;

        case LAIK_AT_BufInit:
            as->initOpCount += ((Laik_BackendAction*)a)->count;
            break;

        case LAIK_AT_RBufCopy:
        case LAIK_AT_BufCopy:
        case LAIK_AT_PackToRBuf:
        case LAIK_AT_PackToBuf:
        case LAIK_AT_MapPackToRBuf:
        case LAIK_AT_MapPackToBuf:
        case LAIK_AT_UnpackFromRBuf:
        case LAIK_AT_UnpackFromBuf:
        case LAIK_AT_MapUnpackFromRBuf:
        case LAIK_AT_MapUnpackFromBuf:
            as->byteBufCopyCount += ((Laik_BackendAction*)a)->count * tc->data->elemsize;
            break;

        case LAIK_AT_CopyToBuf:
        case LAIK_AT_CopyToRBuf:
        case LAIK_AT_CopyFromBuf:
        case LAIK_AT_CopyFromRBuf:
            count = ((Laik_BackendAction*)a)->count;
            ce = ((Laik_BackendAction*)a)->ce;
            for(unsigned int i = 0; i < count; i++)
                as->byteBufCopyCount += ce[i].bytes;
            break;

        default:
            not_processed++;
            break;
        }
    }

    return not_processed;
}

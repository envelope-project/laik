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

// TODO: rename to ActionSeq, start with empty, and appendTransition()
Laik_ActionSeq* laik_actions_new(Laik_Instance *inst)
{
    Laik_ActionSeq* as = malloc(sizeof(Laik_ActionSeq));
    as->inst = inst;

    for(int i = 0; i < CONTEXTS_MAX; i++)
        as->context[i] = 0;

    for(int i = 0; i < BUFFER_MAX; i++) {
        as->buf[i] = 0;
        as->bufSize[i] = 0;
    }
    as->currentBuf = 0;
    as->bufReserveCount = 0;

    as->ce = 0;

    as->actionCount = 0;
    as->actionAllocCount = 0;
    as->action = 0;

    as->sendCount = 0;
    as->recvCount = 0;
    as->reduceCount = 0;

    return as;
}


void laik_actions_free(Laik_ActionSeq* as)
{
    for(int i = 0; i < CONTEXTS_MAX; i++)
        free(as->context[i]);

    for(int i = 0; i < BUFFER_MAX; i++)
        free(as->buf[i]);

    free(as->ce);

    free(as->action);
    free(as);
}

Laik_BackendAction* laik_actions_addAction(Laik_ActionSeq* as)
{
    if (as->actionCount == as->actionAllocCount) {
        // enlarge buffer
        as->actionAllocCount = (as->actionCount + 20) * 2;
        as->action = realloc(as->action,
                             as->actionAllocCount * sizeof(Laik_BackendAction));
        if (!as->action) {
            laik_panic("Out of memory allocating memory for Laik_TransitionPlan");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    Laik_BackendAction* a = &(as->action[as->actionCount]);
    as->actionCount++;

    a->type = LAIK_AT_Invalid;
    a->len = sizeof(Laik_BackendAction);
    a->tid = 0; // always refer to single transition context
    a->round = 0;

    return a;
}

void laik_actions_initTContext(Laik_TransitionContext* tc,
                               Laik_Data* data, Laik_Transition* transition,
                               Laik_MappingList* fromList,
                               Laik_MappingList* toList)
{
    tc->data = data;
    tc->transition = transition;
    tc->fromList = fromList;
    tc->toList = toList;
}

int laik_actions_addTContext(Laik_ActionSeq* as,
                             Laik_Data* data, Laik_Transition* transition,
                             Laik_MappingList* fromList,
                             Laik_MappingList* toList)
{
    Laik_TransitionContext* tc = malloc(sizeof(Laik_TransitionContext));
    laik_actions_initTContext(tc, data, transition, fromList, toList);

    assert(as->context[0] == 0);
    as->context[0] = tc;

    return 0;
}

// append action to reserve buffer space
// if <bufID> is negative, a new ID is generated (always > 100)
// returns bufID.
//
// bufID < 100 are reserved for buffers already allocated (buf[bufID]).
// in a final pass, all buffer reservations must be collected, the buffer
// allocated (with ID 0), and the references to this buffer replaced
// by references into buffer 0. These actions can be removed afterwards.
int laik_actions_addBufReserve(Laik_ActionSeq* as, int size, int bufID)
{
    if (bufID < 0) {
        // generate new buf ID
        // only return IDs > 0. ID 0 is reserved for actually allocated buffer
        bufID = as->bufReserveCount + 100;
        as->bufReserveCount++;
    }
    else if (bufID > as->bufReserveCount + 99)
        as->bufReserveCount = bufID - 99;

    Laik_BackendAction* a = laik_actions_addAction(as);
    a->type = LAIK_AT_BufReserve;
    a->count = size;
    a->bufID = bufID;
    a->offset = 0;

    return bufID;
}

// append send action to buffer referencing a previous reserve action
void laik_actions_addRBufSend(Laik_ActionSeq* as, int round,
                              int bufID, int byteOffset,
                              int count, int to)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_RBufSend;
    a->round = round;
    a->bufID = bufID;
    a->offset = byteOffset;
    a->count = count;
    a->peer_rank = to;
}

// append recv action into buffer referencing a previous reserve action
void laik_actions_addRBufRecv(Laik_ActionSeq* as, int round,
                              int bufID, int byteOffset,
                              int count, int from)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_RBufRecv;
    a->round = round;
    a->bufID = bufID;
    a->offset = byteOffset;
    a->count = count;
    a->peer_rank = from;
}

// append action to call a local reduce operation
// using buffer referenced by a previous reserve action and toBuf as input
void laik_actions_addRBufLocalReduce(Laik_ActionSeq* as, int round,
                                     Laik_Type* dtype,
                                     Laik_ReductionOperation redOp,
                                     int fromBufID, int fromByteOffset,
                                     char* toBuf, int count)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_RBufLocalReduce;
    a->round = round;
    a->dtype = dtype;
    a->redOp = redOp;
    a->toBuf = toBuf;
    a->count = count;
    a->bufID = fromBufID;
    a->offset = fromByteOffset;
}

// append action to call an init operation
void laik_actions_addBufInit(Laik_ActionSeq* as, int round,
                             Laik_Type* dtype,
                             Laik_ReductionOperation redOp,
                             char* toBuf, int count)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_BufInit;
    a->round = round;
    a->dtype = dtype;
    a->redOp = redOp;
    a->toBuf = toBuf;
    a->count = count;
}

// append action to call a copy operation from/to a buffer
// if fromBuf is 0, use a buffer referenced by a previous reserve action
void laik_actions_addRBufCopy(Laik_ActionSeq* as, int round,
                              int fromBufID, int fromByteOffset,
                              char* toBuf, int count)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_RBufCopy;
    a->round = round;
    a->bufID = fromBufID;
    a->offset = fromByteOffset;
    a->toBuf = toBuf;
    a->count = count;
}

// append action to call a copy operation from/to a buffer
void laik_actions_addBufCopy(Laik_ActionSeq* as, int round,
                             char* fromBuf, char* toBuf, int count)
{
    assert(fromBuf != toBuf);

    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_BufCopy;
    a->round = round;
    a->fromBuf = fromBuf;
    a->toBuf = toBuf;
    a->count = count;
}


// append send action from a mapping with offset
void laik_actions_addMapSend(Laik_ActionSeq* as, int round,
                             int fromMapNo, uint64_t off,
                             int count, int to)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_MapSend;
    a->round = round;
    a->mapNo = fromMapNo;
    a->offset = off;
    a->count = count;
    a->peer_rank = to;

    as->sendCount += count;
}

// append send action from a buffer
void laik_actions_addBufSend(Laik_ActionSeq* as, int round,
                             char* fromBuf, int count, int to)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_BufSend;
    a->round = round;
    a->fromBuf = fromBuf;
    a->count = count;
    a->peer_rank = to;

    as->sendCount += count;
}

// append recv action into a mapping with offset
void laik_actions_addMapRecv(Laik_ActionSeq* as, int round,
                             int toMapNo, uint64_t off,
                             int count, int from)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_MapRecv;
    a->round = round;
    a->mapNo = toMapNo;
    a->offset = off;
    a->count = count;
    a->peer_rank = from;

    as->recvCount += count;
}

// append recv action into a buffer
void laik_actions_addBufRecv(Laik_ActionSeq* as, int round,
                             char* toBuf, int count, int from)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_BufRecv;
    a->round = round;
    a->toBuf = toBuf;
    a->count = count;
    a->peer_rank = from;

    as->recvCount += count;
}


void laik_actions_initPackAndSend(Laik_BackendAction* a, int round,
                                  Laik_Mapping* fromMap, int dims, Laik_Slice* slc,
                                  int to)
{
    a->type = LAIK_AT_PackAndSend;
    a->round = round;
    a->map = fromMap;
    a->dims = dims;
    a->slc = slc;
    a->peer_rank = to;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}

void laik_actions_addPackAndSend(Laik_ActionSeq* as, int round,
                                 Laik_Mapping* fromMap, Laik_Slice* slc, int to)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;
    laik_actions_initPackAndSend(a, round, fromMap, dims, slc, to);

    as->sendCount += a->count;
}

void laik_actions_initMapPackAndSend(Laik_BackendAction* a, int round,
                                     int fromMapNo, int dims, Laik_Slice* slc,
                                     int to)
{
    a->type = LAIK_AT_MapPackAndSend;
    a->round = round;
    a->mapNo = fromMapNo;
    a->dims = dims;
    a->slc = slc;
    a->peer_rank = to;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}

void laik_actions_addMapPackAndSend(Laik_ActionSeq* as, int round,
                                    int fromMapNo, Laik_Slice* slc, int to)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;
    laik_actions_initMapPackAndSend(a, round, fromMapNo, dims, slc, to);

    as->sendCount += a->count;
}


void laik_actions_initRecvAndUnpack(Laik_BackendAction* a, int round,
                                    Laik_Mapping* toMap, int dims, Laik_Slice* slc,
                                    int from)
{
    a->type = LAIK_AT_RecvAndUnpack;
    a->round = round;
    a->map = toMap;
    a->dims = dims;
    a->slc = slc;
    a->peer_rank = from;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}

void laik_actions_addRecvAndUnpack(Laik_ActionSeq* as, int round,
                                   Laik_Mapping* toMap, Laik_Slice* slc, int from)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;
    laik_actions_initRecvAndUnpack(a, round, toMap, dims, slc, from);

    as->recvCount += a->count;
}

void laik_actions_initMapRecvAndUnpack(Laik_BackendAction* a, int round,
                                       int toMapNo, int dims, Laik_Slice* slc,
                                       int from)
{
    a->type = LAIK_AT_MapRecvAndUnpack;
    a->round = round;
    a->mapNo = toMapNo;
    a->dims = dims;
    a->slc = slc;
    a->peer_rank = from;
    a->count = laik_slice_size(dims, slc);
    assert(a->count > 0);
}

void laik_actions_addMapRecvAndUnpack(Laik_ActionSeq* as, int round,
                                      int toMapNo, Laik_Slice* slc, int from)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    Laik_TransitionContext* tc = as->context[0];
    int dims = tc->transition->space->dims;
    laik_actions_initMapRecvAndUnpack(a, round, toMapNo, dims, slc, from);

    as->recvCount += a->count;
}


void laik_actions_initReduce(Laik_BackendAction* a,
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


void laik_actions_addReduce(Laik_ActionSeq* as,
                            char* fromBuf, char* toBuf, int count,
                            int rootTask, Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_actions_addAction(as);
    laik_actions_initReduce(a, fromBuf, toBuf, count, rootTask, redOp);

    assert(count > 0);
    as->reduceCount += count;
}

void laik_actions_addRBufReduce(Laik_ActionSeq* as,
                                int bufID, int byteOffset, int count,
                                int rootTask, Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_actions_addAction(as);
    a->type = LAIK_AT_RBufReduce;
    a->bufID = bufID;
    a->offset = byteOffset;
    a->count = count;
    a->peer_rank = rootTask;
    a->redOp = redOp;

    assert(count > 0);
    as->reduceCount += count;
}


void laik_actions_initGroupReduce(Laik_BackendAction* a,
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

void laik_actions_addGroupReduce(Laik_ActionSeq* as,
                                 int inputGroup, int outputGroup,
                                 char* fromBuf, char* toBuf, int count,
                                 Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_actions_addAction(as);
    laik_actions_initGroupReduce(a, inputGroup, outputGroup,
                                 fromBuf, toBuf, count, redOp);

    assert(count > 0);
    as->reduceCount += count;
}

// similar to addGroupReduce
void laik_actions_addRBufGroupReduce(Laik_ActionSeq* as,
                                     int inputGroup, int outputGroup,
                                     int bufID, int byteOffset, int count,
                                     Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

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



void laik_actions_addCopyToBuf(Laik_ActionSeq* as, int round,
                               Laik_CopyEntry* ce, char* toBuf, int count)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_CopyToBuf;
    a->round = round;
    a->ce = ce;
    a->toBuf = toBuf;
    a->count = count;
}

void laik_actions_addCopyFromBuf(Laik_ActionSeq* as, int round,
                                 Laik_CopyEntry* ce, char* fromBuf, int count)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_CopyFromBuf;
    a->round = round;
    a->ce = ce;
    a->fromBuf = fromBuf;
    a->count = count;
}

void laik_actions_addCopyToRBuf(Laik_ActionSeq* as, int round,
                                Laik_CopyEntry* ce,
                                int toBufID, int toByteOffset, int count)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_CopyToRBuf;
    a->round = round;
    a->ce = ce;
    a->bufID = toBufID;
    a->offset = toByteOffset;
    a->count = count;

}

void laik_actions_addCopyFromRBuf(Laik_ActionSeq* as, int round,
                                  Laik_CopyEntry* ce,
                                  int fromBufID, int fromByteOffset, int count)
{
    Laik_BackendAction* a = laik_actions_addAction(as);

    a->type = LAIK_AT_CopyFromRBuf;
    a->round = round;
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


// add all receive ops from a transition to an ActionSeq.
// before execution, a deadlock-avoidance action sorting is required
void laik_actions_addRecvs(Laik_ActionSeq* as, int round,
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
void laik_actions_addSends(Laik_ActionSeq* as, int round,
                           Laik_Data* data, Laik_Transition* t)
{
    Laik_TransitionContext* tc = as->context[0];
    assert(tc->data == data);
    assert(tc->transition == t);
    assert(t->group->myid >= 0);


    for(int i=0; i < t->sendCount; i++) {
        struct sendTOp* op = &(t->send[i]);
        laik_actions_addMapPackAndSend(as, round, op->mapNo,
                                       &(op->slc), op->toTask);
    }
}



// collect buffer reservation actions and update actions referencing them
// works in-place, only call once
void laik_actions_allocBuffer(Laik_ActionSeq* as)
{
    int rCount = 0, rActions = 0;
    assert(as->currentBuf < BUFFER_MAX);
    assert(as->buf[as->currentBuf] == 0); // nothing allocated yet

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
            if (ba->bufID < BUFFER_MAX) break;

            ba->offset = bufSize;
            assert(ba->bufID >= 100);
            assert(ba->bufID < 100 + as->bufReserveCount);
            resAction[ba->bufID - 100] = ba;
            ba->bufID = as->currentBuf; // mark as processed
            bufSize += ba->count;
            rCount++;
            break;

        case LAIK_AT_RBufCopy:
        case LAIK_AT_RBufLocalReduce:
        case LAIK_AT_RBufSend:
        case LAIK_AT_RBufRecv:
        case LAIK_AT_RBufReduce:
        case LAIK_AT_CopyFromRBuf:
        case LAIK_AT_CopyToRBuf:
        case LAIK_AT_RBufGroupReduce: {
            // action with allocated reservation
            if (ba->bufID < BUFFER_MAX) break;

            assert(ba->bufID >= 100);
            assert(ba->bufID < 100 + as->bufReserveCount);
            Laik_BackendAction* ra = resAction[ba->bufID - 100];
            assert(ra != 0);
            assert(ba->count > 0);
            assert(ba->offset + (uint64_t)(ba->count * elemsize) <= (uint64_t) ra->count);

            ba->offset += ra->offset;
            ba->bufID = as->currentBuf; // reference into allocated buffer
            rActions++;
            break;
        }

        default:
            break;
        }
    }
    free(resAction);

    if (bufSize == 0) {
        laik_log(1, "RBuf alloc %d: Nothing to do.", as->currentBuf + 1);
        return;
    }

    char* buf = malloc(bufSize);
    assert(buf != 0);

    // eventually modify actions, now that buffer allocation is known
    for(int i = 0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);
        switch(ba->type) {
        case LAIK_AT_RBufSend:
            ba->fromBuf = buf + ba->offset;
            ba->type = LAIK_AT_BufSend;
            break;
        case LAIK_AT_RBufRecv:
            ba->toBuf = buf + ba->offset;
            ba->type = LAIK_AT_BufRecv;
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
    as->bufSize[as->currentBuf] = bufSize;
    as->buf[as->currentBuf] = buf;

    laik_log(1, "RBuf alloc %d: %d reservations, %d RBuf actions => %llu bytes at %p",
             as->currentBuf + 1, rCount, rActions, (long long unsigned) bufSize,
             (void*) as->buf[as->currentBuf]);
    assert(rCount == as->bufReserveCount);

    // start again with bufID 100 for next reservations
    as->bufReserveCount = 0;
    as->currentBuf++;
}


// returns a new empty action sequence with same transition context
Laik_ActionSeq* laik_actions_setupTransform(Laik_ActionSeq* oldAS)
{
    Laik_TransitionContext* tc = oldAS->context[0];
    Laik_Data* d = tc->data;
    Laik_ActionSeq* as = laik_actions_new(d->space->inst);
    laik_actions_addTContext(as, d, tc->transition,
                             tc->fromList, tc->toList);

    // take over already allocated buffers
    for(int i = 0; i < BUFFER_MAX; i++) {
        as->buf[i] = oldAS->buf[i];
        oldAS->buf[i] = 0; // do not double-free (cannot exec oldAS anymore!)
        as->bufSize[i] = oldAS->bufSize[i];
    }
    as->currentBuf = oldAS->currentBuf;
    // skip already used bufIDs for reservation
    as->bufReserveCount = oldAS->bufReserveCount;

    // take care of CopyEntries
    as->ce = oldAS->ce;
    oldAS->ce = 0;

    return as;
}

// append actions to <as>
void laik_actions_add(Laik_BackendAction* ba, Laik_ActionSeq* as)
{
    switch(ba->type) {
    case LAIK_AT_Nop:
        // no need to copy a NOP operation
        break;

    case LAIK_AT_BufReserve:
        // can ignored if already processed
        if (ba->bufID < BUFFER_MAX) break;

        laik_actions_addBufReserve(as, ba->count, ba->bufID);
        break;

    case LAIK_AT_MapSend:
        laik_actions_addMapSend(as, ba->round, ba->mapNo, ba->offset,
                                ba->count, ba->peer_rank);
        break;

    case LAIK_AT_BufSend:
        laik_actions_addBufSend(as, ba->round,
                                ba->fromBuf, ba->count, ba->peer_rank);
        break;

    case LAIK_AT_RBufSend:
        laik_actions_addRBufSend(as, ba->round, ba->bufID, ba->offset,
                                 ba->count, ba->peer_rank);
        break;

    case LAIK_AT_MapRecv:
        laik_actions_addMapRecv(as, ba->round, ba->mapNo, ba->offset,
                                ba->count, ba->peer_rank);
        break;

    case LAIK_AT_BufRecv:
        laik_actions_addBufRecv(as, ba->round,
                                ba->toBuf, ba->count, ba->peer_rank);
        break;

    case LAIK_AT_RBufRecv:
        laik_actions_addRBufRecv(as, ba->round, ba->bufID, ba->offset,
                                 ba->count, ba->peer_rank);
        break;

    case LAIK_AT_MapPackAndSend:
        laik_actions_addMapPackAndSend(as, ba->round,
                                       ba->mapNo, ba->slc, ba->peer_rank);
        break;

    case LAIK_AT_PackAndSend:
        laik_actions_addPackAndSend(as, ba->round,
                                    ba->map, ba->slc, ba->peer_rank);
        break;

    case LAIK_AT_MapRecvAndUnpack:
        laik_actions_addMapRecvAndUnpack(as, ba->round,
                                         ba->mapNo, ba->slc, ba->peer_rank);
        break;

    case LAIK_AT_RecvAndUnpack:
        laik_actions_addRecvAndUnpack(as, ba->round,
                                      ba->map, ba->slc, ba->peer_rank);
        break;

    case LAIK_AT_BufCopy:
        laik_actions_addBufCopy(as, ba->round,
                                ba->fromBuf, ba->toBuf, ba->count);
        break;

    case LAIK_AT_RBufCopy:
        laik_actions_addRBufCopy(as, ba->round, ba->bufID, ba->offset,
                                 ba->toBuf, ba->count);
        break;

    case LAIK_AT_RBufReduce:
        laik_actions_addRBufReduce(as, ba->bufID, ba->offset, ba->count,
                                   ba->peer_rank, ba->redOp);
        break;

    case LAIK_AT_Reduce:
        laik_actions_addReduce(as, ba->fromBuf, ba->toBuf, ba->count,
                               ba->peer_rank, ba->redOp);
        break;

    case LAIK_AT_GroupReduce:
        laik_actions_addGroupReduce(as,
                                    ba->inputGroup, ba->outputGroup,
                                    ba->fromBuf, ba->toBuf,
                                    ba->count, ba->redOp);
        break;

    case LAIK_AT_RBufLocalReduce:
        laik_actions_addRBufLocalReduce(as, ba->round, ba->dtype, ba->redOp,
                                        ba->bufID, ba->offset,
                                        ba->toBuf, ba->count);
        break;

    case LAIK_AT_BufInit:
        laik_actions_addBufInit(as, ba->round, ba->dtype, ba->redOp,
                                ba->toBuf, ba->count);
        break;

    default:
        laik_log(LAIK_LL_Panic,
                 "laik_actions_add: unknown action %d", ba->type);
        assert(0);
    }
}


// just copy actions from oldAS into as
void laik_actions_copySeq(Laik_ActionSeq* oldAS, Laik_ActionSeq* as)
{
    for(int i = 0; i < oldAS->actionCount; i++) {
        Laik_BackendAction* ba = &(oldAS->action[i]);
        laik_actions_add(ba, as);
    }
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
void laik_actions_combineActions(Laik_ActionSeq* oldAS, Laik_ActionSeq* as)
{
    int combineGroupReduce = 1;
    int combineReduce = 1;

    Laik_TransitionContext* tc = oldAS->context[0];
    Laik_Data* d = tc->data;
    int elemsize = d->elemsize;
    // used for combining GroupReduce actions
    int myid = tc->transition->group->myid;

    // unmark all actions first
    // all actions will be marked on combining, to not process them twice
    for(int i = 0; i < oldAS->actionCount; i++)
        oldAS->action[i].mark = 0;

    // first pass: how much buffer space / copy range elements is needed?
    int bufSize = 0, copyRanges = 0;
    int round, rank, j, count, actionCount;
    for(int i = 0; i < oldAS->actionCount; i++) {
        Laik_BackendAction* ba = &(oldAS->action[i]);

        // skip already combined actions
        if (ba->mark == 1) continue;

        switch(ba->type) {
        case LAIK_AT_BufSend:
            // combine all BufSend actions in same round with same target rank
            rank = ba->peer_rank;
            round = ba->round;

            count = ba->count;
            actionCount = 1;
            for(j = i+1; j < oldAS->actionCount; j++) {
                if (oldAS->action[j].type != LAIK_AT_BufSend) continue;
                if (oldAS->action[j].peer_rank != rank) continue;
                if (oldAS->action[j].round != round) continue;

                // should be unmarked
                assert(oldAS->action[j].mark == 0);
                oldAS->action[j].mark = 1;

                count += oldAS->action[j].count;
                actionCount++;
            }
            if (actionCount > 1) {
                bufSize += count;
                copyRanges += actionCount;
            }
            break;

        case LAIK_AT_BufRecv:
            // combine all BufRecv actions in same round with same source rank
            rank = ba->peer_rank;
            round = ba->round;

            count = ba->count;
            actionCount = 1;
            for(j = i+1; j < oldAS->actionCount; j++) {
                if (oldAS->action[j].type != LAIK_AT_BufRecv) continue;
                if (oldAS->action[j].peer_rank != rank) continue;
                if (oldAS->action[j].round != round) continue;

                // should be unmarked
                assert(oldAS->action[j].mark == 0);
                oldAS->action[j].mark = 1;

                count += oldAS->action[j].count;
                actionCount++;
            }
            if (actionCount > 1) {
                bufSize += count;
                copyRanges += actionCount;
            }
            break;

        case LAIK_AT_GroupReduce: {
            if (!combineGroupReduce) break;

            // combine all GroupReduce actions with same
            // inputGroup, outputGroup, and redOp
            count = ba->count;
            int inputGroup = ba->inputGroup;
            int outputGroup = ba->outputGroup;
            Laik_ReductionOperation redOp = ba->redOp;
            int actionCount = 1;
            for(j = i+1; j < oldAS->actionCount; j++) {
                if (oldAS->action[j].type != LAIK_AT_GroupReduce) continue;
                if (oldAS->action[j].inputGroup != inputGroup) continue;
                if (oldAS->action[j].outputGroup != outputGroup) continue;
                if (oldAS->action[j].redOp != redOp) continue;
                // should be unmarked
                assert(oldAS->action[j].mark == 0);
                oldAS->action[j].mark = 1;
                count += oldAS->action[j].count;
                actionCount++;
            }
            if (actionCount > 1) {
                bufSize += count;
                if (laik_isInGroup(tc->transition, inputGroup, myid))
                    copyRanges += actionCount;
                if (laik_isInGroup(tc->transition, outputGroup, myid))
                    copyRanges += actionCount;
            }
            break;
        }

        case LAIK_AT_Reduce: {
            if (!combineReduce) break;

            // combine all reduce actions with same root and redOp
            count = ba->count;
            int root = ba->peer_rank;
            Laik_ReductionOperation redOp = ba->redOp;
            int actionCount = 1;
            for(j = i+1; j < oldAS->actionCount; j++) {
                if (oldAS->action[j].type != LAIK_AT_Reduce) continue;
                if (oldAS->action[j].peer_rank != root) continue;
                if (oldAS->action[j].redOp != redOp) continue;
                // should be unmarked
                assert(oldAS->action[j].mark == 0);
                oldAS->action[j].mark = 1;
                count += oldAS->action[j].count;
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
        laik_log(1, "Combining action sequence: nothing to do.");
        laik_actions_copySeq(oldAS, as);
        return;
    }

    assert(copyRanges > 0);
    assert(as->ce == 0);  // ensure no entries yet allocated

    int bufID = laik_actions_addBufReserve(as, bufSize * elemsize, -1);

    as->ce = malloc(copyRanges * sizeof(Laik_CopyEntry));

    laik_log(1, "Reservation for combined actions: length %d x %d, ranges %d",
             bufSize, elemsize, copyRanges);

    // unmark all actions: restart for finding same type of actions
    for(int i = 0; i < oldAS->actionCount; i++)
        oldAS->action[i].mark = 0;

    // second pass: add merged actions
    int bufOff = 0;
    int rangeOff = 0;

    for(int i = 0; i < oldAS->actionCount; i++) {
        Laik_BackendAction* ba = &(oldAS->action[i]);

        // skip already processed actions
        if (ba->mark == 1) continue;

        switch(ba->type) {
        case LAIK_AT_BufSend:
            rank = ba->peer_rank;
            round = ba->round;

            count = ba->count;
            actionCount = 1;
            for(j = i+1; j < oldAS->actionCount; j++) {
                if (oldAS->action[j].type != LAIK_AT_BufSend) continue;
                if (oldAS->action[j].peer_rank != rank) continue;
                if (oldAS->action[j].round != round) continue;

                assert(oldAS->action[j].mark == 0);
                oldAS->action[j].mark = 1;

                count += oldAS->action[j].count;
                actionCount++;
            }
            if (actionCount > 1) {
                //laik_log(1,"Send Seq %d - %d, rangeOff %d, bufOff %d, count %d",
                //         i, j, rangeOff, bufOff, count);
                laik_actions_addCopyToRBuf(as, ba->round,
                                           as->ce + rangeOff,
                                           bufID, 0,
                                           actionCount);
                laik_actions_addRBufSend(as, ba->round,
                                         bufID, bufOff * elemsize,
                                         count, rank);
                int oldRangeOff = rangeOff;
                for(int k = i; k < oldAS->actionCount; k++) {
                    if (oldAS->action[k].type != LAIK_AT_BufSend) continue;
                    if (oldAS->action[k].peer_rank != rank) continue;
                    if (oldAS->action[k].round != round) continue;

                    assert(rangeOff < copyRanges);
                    as->ce[rangeOff].ptr = oldAS->action[k].fromBuf;
                    as->ce[rangeOff].bytes = oldAS->action[k].count * elemsize;
                    as->ce[rangeOff].offset = bufOff * elemsize;
                    bufOff += oldAS->action[k].count;
                    rangeOff++;
                }
                assert(oldRangeOff + actionCount == rangeOff);
            }
            else
                laik_actions_addBufSend(as, ba->round,
                                        ba->fromBuf, count, rank);
            break;

        case LAIK_AT_BufRecv:
            rank = ba->peer_rank;
            round = ba->round;

            count = ba->count;
            actionCount = 1;
            for(j = i+1; j < oldAS->actionCount; j++) {
                if (oldAS->action[j].type != LAIK_AT_BufRecv) continue;
                if (oldAS->action[j].peer_rank != rank) continue;
                if (oldAS->action[j].round != round) continue;

                assert(oldAS->action[j].mark == 0);
                oldAS->action[j].mark = 1;

                count += oldAS->action[j].count;
                actionCount++;
            }
            if (actionCount > 1) {
                laik_actions_addRBufRecv(as, ba->round,
                                         bufID, bufOff * elemsize,
                                         count, rank);
                laik_actions_addCopyFromRBuf(as, ba->round,
                                             as->ce + rangeOff,
                                             bufID, 0,
                                             actionCount);
                int oldRangeOff = rangeOff;
                for(int k = i; k < oldAS->actionCount; k++) {
                    if (oldAS->action[k].type != LAIK_AT_BufRecv) continue;
                    if (oldAS->action[k].peer_rank != rank) continue;
                    if (oldAS->action[k].round != round) continue;

                    assert(rangeOff < copyRanges);
                    as->ce[rangeOff].ptr = oldAS->action[k].toBuf;
                    as->ce[rangeOff].bytes = oldAS->action[k].count * elemsize;
                    as->ce[rangeOff].offset = bufOff * elemsize;
                    bufOff += oldAS->action[k].count;
                    rangeOff++;
                }
                assert(oldRangeOff + actionCount == rangeOff);
            }
            else
                laik_actions_addBufRecv(as, ba->round,
                                        ba->toBuf, count, rank);
            break;

        case LAIK_AT_GroupReduce: {
            if (!combineGroupReduce) {
                // pass through
                laik_actions_addGroupReduce(as,
                                            ba->inputGroup, ba->outputGroup,
                                            ba->fromBuf, ba->toBuf,
                                            ba->count, ba->redOp);
                break;
            }

            count = ba->count;
            int inputGroup = ba->inputGroup;
            int outputGroup = ba->outputGroup;
            Laik_ReductionOperation redOp = ba->redOp;
            int actionCount = 1;
            for(j = i+1; j < oldAS->actionCount; j++) {
                if (oldAS->action[j].type != LAIK_AT_GroupReduce) continue;
                if (oldAS->action[j].inputGroup != inputGroup) continue;
                if (oldAS->action[j].outputGroup != outputGroup) continue;
                if (oldAS->action[j].redOp != redOp) continue;

                // should be unmarked
                assert(oldAS->action[j].mark == 0);
                oldAS->action[j].mark = 1;

                count += oldAS->action[j].count;
                actionCount++;
            }
            if (actionCount > 1) {
                // temporary buffer used as input and output for reduce
                int startBufOff = bufOff;

                // if I provide input: copy pieces into temporary buffer
                if (laik_isInGroup(tc->transition, inputGroup, myid)) {
                    laik_actions_addCopyToRBuf(as, ba->round,
                                               as->ce + rangeOff,
                                               bufID, 0,
                                               actionCount);
                    // ranges for input pieces
                    int oldRangeOff = rangeOff;
                    for(int k = i; k < oldAS->actionCount; k++) {
                        if (oldAS->action[k].type != LAIK_AT_GroupReduce) continue;
                        if (oldAS->action[k].inputGroup != inputGroup) continue;
                        if (oldAS->action[k].outputGroup != outputGroup) continue;
                        if (oldAS->action[k].redOp != redOp) continue;

                        assert(rangeOff < copyRanges);
                        as->ce[rangeOff].ptr = oldAS->action[k].fromBuf;
                        as->ce[rangeOff].bytes = oldAS->action[k].count * elemsize;
                        as->ce[rangeOff].offset = bufOff * elemsize;
                        bufOff += oldAS->action[k].count;
                        rangeOff++;
                    }
                    assert(oldRangeOff + actionCount == rangeOff);
                }

                // use temporary buffer for both input and output
                laik_actions_addRBufGroupReduce(as,
                                                inputGroup, outputGroup,
                                                bufID, startBufOff * elemsize,
                                                count, redOp);

                // if I want output: copy pieces from temporary buffer
                if (laik_isInGroup(tc->transition, outputGroup, myid)) {
                    laik_actions_addCopyFromRBuf(as, ba->round,
                                                 as->ce + rangeOff,
                                                 bufID, 0,
                                                 actionCount);
                    bufOff = startBufOff;
                    int oldRangeOff = rangeOff;
                    for(int k = i; k < oldAS->actionCount; k++) {
                        if (oldAS->action[k].type != LAIK_AT_GroupReduce) continue;
                        if (oldAS->action[k].inputGroup != inputGroup) continue;
                        if (oldAS->action[k].outputGroup != outputGroup) continue;
                        if (oldAS->action[k].redOp != redOp) continue;

                        assert(rangeOff < copyRanges);
                        as->ce[rangeOff].ptr = oldAS->action[k].toBuf;
                        as->ce[rangeOff].bytes = oldAS->action[k].count * elemsize;
                        as->ce[rangeOff].offset = bufOff * elemsize;
                        bufOff += oldAS->action[k].count;
                        rangeOff++;
                    }
                    assert(oldRangeOff + actionCount == rangeOff);
                }
                bufOff = startBufOff + count;
            }
            else
                laik_actions_addGroupReduce(as,
                                            ba->inputGroup, ba->outputGroup,
                                            ba->fromBuf, ba->toBuf,
                                            ba->count, ba->redOp);
            break;
        }

        case LAIK_AT_Reduce: {
            if (!combineReduce) {
                // pass through
                laik_actions_addReduce(as, ba->fromBuf, ba->toBuf,
                                       ba->count, ba->peer_rank, ba->redOp);
                break;
            }

            count = ba->count;
            int root = ba->peer_rank;
            Laik_ReductionOperation redOp = ba->redOp;
            int actionCount = 1;
            for(j = i+1; j < oldAS->actionCount; j++) {
                if (oldAS->action[j].type != LAIK_AT_Reduce) continue;
                if (oldAS->action[j].peer_rank != root) continue;
                if (oldAS->action[j].redOp != redOp) continue;
                // should be unmarked
                assert(oldAS->action[j].mark == 0);
                oldAS->action[j].mark = 1;
                count += oldAS->action[j].count;
                actionCount++;
            }
            if (actionCount > 1) {
                // temporary buffer used as input and output for reduce
                int startBufOff = bufOff;

                // copy input pieces into temporary buffer
                laik_actions_addCopyToRBuf(as, ba->round,
                                           as->ce + rangeOff,
                                           bufID, 0,
                                           actionCount);
                // ranges for input pieces
                int oldRangeOff = rangeOff;
                for(int k = i; k < oldAS->actionCount; k++) {
                    if (oldAS->action[k].type != LAIK_AT_Reduce) continue;
                    if (oldAS->action[k].peer_rank != root) continue;
                    if (oldAS->action[k].redOp != redOp) continue;

                    assert(rangeOff < copyRanges);
                    as->ce[rangeOff].ptr = oldAS->action[k].fromBuf;
                    as->ce[rangeOff].bytes = oldAS->action[k].count * elemsize;
                    as->ce[rangeOff].offset = bufOff * elemsize;
                    bufOff += oldAS->action[k].count;
                    rangeOff++;
                }
                assert(oldRangeOff + actionCount == rangeOff);
                assert(startBufOff + count == bufOff);

                // use temporary buffer for both input and output
                laik_actions_addRBufReduce(as, bufID, startBufOff * elemsize,
                                           count, root, redOp);

                // if I want result, copy output ranges
                if ((root == myid) || (root == -1)) {
                    // collect output ranges: we cannot reuse copy ranges from
                    // input pieces because of potentially other output buffers
                    laik_actions_addCopyFromRBuf(as, ba->round,
                                                 as->ce + rangeOff,
                                                 bufID, 0,
                                                 actionCount);
                    bufOff = startBufOff;
                    int oldRangeOff = rangeOff;
                    for(int k = i; k < oldAS->actionCount; k++) {
                        if (oldAS->action[k].type != LAIK_AT_Reduce) continue;
                        if (oldAS->action[k].peer_rank != root) continue;
                        if (oldAS->action[k].redOp != redOp) continue;

                        assert(rangeOff < copyRanges);
                        as->ce[rangeOff].ptr = oldAS->action[k].toBuf;
                        as->ce[rangeOff].bytes = oldAS->action[k].count * elemsize;
                        as->ce[rangeOff].offset = bufOff * elemsize;
                        bufOff += oldAS->action[k].count;
                        rangeOff++;
                    }
                    assert(oldRangeOff + actionCount == rangeOff);
                }
                bufOff = startBufOff + count;
            }
            else
                laik_actions_addReduce(as, ba->fromBuf, ba->toBuf,
                                       ba->count, ba->peer_rank, ba->redOp);
            break;
        }

        default:
            // pass through
            laik_actions_add(ba, as);
            break;
        }
    }
    assert(rangeOff == copyRanges);
    assert(bufSize == bufOff);
}

/* sort2phase: sort actions into phases to avoid deadlocks
 *
 * - phase 1.X: receive from lower rank <X>
 * - phase 2.X: send to higher rank  <X>
 * - phase 3.X: send to lower rank <X>
 * - phase 4.X: receive from higher rank <X>
 *
 * for sends/recvs among same peers, order must be kept,
 * use stable sort for that
*/

// used by compare function, set directly before sort
static int myid4cmp;

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

// sort send/recv actions to avoid deadlocks within each round, others
// are moved to front. Copy resorted sequence into as2
void laik_actions_sort2phase(Laik_ActionSeq* as, Laik_ActionSeq* as2)
{
    Laik_BackendAction** order = malloc(as->actionCount * sizeof(void*));

    for(int i=0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);
        order[i] = ba;
    }

    Laik_TransitionContext* tc = as->context[0];
    myid4cmp = tc->transition->group->myid;
    qsort(order, as->actionCount, sizeof(void*), cmp2phase);

    // add actions in new order to as2
    for(int i = 0; i < as->actionCount; i++) {
        laik_actions_add(order[i], as2);
    }

    free(order);
}

// transform MapPackAndSend/MapRecvAndUnpack into simple Send/Recv actions
// if mapping is known and direct send/recv is possible
void laik_actions_flattenPacking(Laik_ActionSeq* as, Laik_ActionSeq* as2)
{
    Laik_Mapping* map;
    int64_t from, to;

    Laik_TransitionContext* tc = as->context[0];

    for(int i = 0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);
        bool handled = false;

        switch(ba->type) {
        case LAIK_AT_MapPackAndSend:
            if (!tc->fromList) break;
            assert(ba->mapNo < tc->fromList->count);
            map = &(tc->fromList->map[ba->mapNo]);

            // TODO: only for 1d for now
            if (ba->dims > 1) break;

            // FIXME: this assumes lexicographical layout
            from = ba->slc->from.i[0] - map->requiredSlice.from.i[0];
            to   = ba->slc->to.i[0] - map->requiredSlice.from.i[0];
            assert(from >= 0);
            assert(to > from);

            // replace with different action depending on map allocation done
            if (map->base)
                laik_actions_addBufSend(as2, ba->round,
                                        map->base + from * tc->data->elemsize,
                                        to - from, ba->peer_rank);
            else
                laik_actions_addMapSend(as2, ba->round,
                                        ba->mapNo, from * tc->data->elemsize,
                                        to - from, ba->peer_rank);
            handled = true;
            break;

        case LAIK_AT_MapRecvAndUnpack:
            if (!tc->toList) break;
            assert(ba->mapNo < tc->toList->count);
            map = &(tc->toList->map[ba->mapNo]);

            // TODO: only for 1d for now
            if (ba->dims > 1) break;

            // FIXME: this assumes lexicographical layout
            from = ba->slc->from.i[0] - map->requiredSlice.from.i[0];
            to   = ba->slc->to.i[0] - map->requiredSlice.from.i[0];
            assert(from >= 0);
            assert(to > from);

            // replace with different action depending on map allocation done
            if (map->base)
                laik_actions_addBufRecv(as2, ba->round,
                                        map->base + from * tc->data->elemsize,
                                        to - from, ba->peer_rank);
            else
                laik_actions_addMapRecv(as2, ba->round,
                                        ba->mapNo, from * tc->data->elemsize,
                                        to - from, ba->peer_rank);
            handled = true;
            break;

        default: break;
        }

        if (!handled)
            laik_actions_add(ba, as2);
    }
}

/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2018 Josef Weidendorfer
 */

#include <laik-internal.h>

#include <assert.h>
#include <stdlib.h>

Laik_TransitionPlan* laik_transplan_new(Laik_Data* d, Laik_Transition* t)
{
    Laik_TransitionPlan* tp = malloc(sizeof(Laik_TransitionPlan));
    tp->data = d;
    tp->transition = t;

    tp->bufCount = 0;
    tp->bufAllocCount = 0;
    tp->buf = 0;

    tp->actionCount = 0;
    tp->actionAllocCount = 0;
    tp->action = 0;

    tp->sendCount = 0;
    tp->recvCount = 0;
    tp->reduceCount = 0;

    return tp;
}

void laik_transplan_free(Laik_TransitionPlan* tp)
{
    if (tp->buf) {
        for(int i = 0; i < tp->bufCount; i++)
            free(tp->buf[i]);
        free(tp->buf);
    }

    free(tp->action);
    free(tp);
}

Laik_BackendAction* laik_transplan_appendAction(Laik_TransitionPlan* tp)
{
    if (tp->actionCount == tp->actionAllocCount) {
        // enlarge buffer
        tp->actionAllocCount = (tp->actionCount + 20) * 2;
        tp->action = realloc(tp->action,
                             tp->actionAllocCount * sizeof(Laik_BackendAction));
        if (!tp->action) {
            laik_panic("Out of memory allocating memory for Laik_TransitionPlan");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    Laik_BackendAction* a = &(tp->action[tp->actionCount]);
    tp->actionCount++;

    a->type = LAIK_AT_Invalid;
    a->len = sizeof(Laik_BackendAction);
    return a;
}

// allocates buffer and appends it list of buffers used for <tp>, returns off
int laik_transplan_appendBuf(Laik_TransitionPlan* tp, int size)
{
    if (tp->bufCount == tp->bufAllocCount) {
        // enlarge buffer
        tp->bufAllocCount = (tp->bufCount + 20) * 2;
        tp->buf = realloc(tp->buf, tp->bufAllocCount * sizeof(char**));
        if (!tp->buf) {
            laik_panic("Out of memory allocating memory for Laik_TransitionPlan");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    char* buf = malloc(size);
    if (buf) {
        laik_panic("Out of memory allocating memory for Laik_TransitionPlan");
        exit(1); // not actually needed, laik_panic never returns
    }
    int bufNo = tp->bufCount;
    tp->buf[bufNo] = buf;
    tp->bufCount++;

    return bufNo;
}

void laik_transplan_recordSend(Laik_TransitionPlan* tp,
                               Laik_Mapping* fromMap, uint64_t off,
                               int count, int to)
{
    Laik_BackendAction* a = laik_transplan_appendAction(tp);
    a->type = LAIK_AT_Send;
    a->map = fromMap;
    a->offset = off;
    a->fromBuf = 0; // not used
    a->count = count;
    a->peer_rank = to;

    tp->sendCount += count;
}

void laik_transplan_recordRecv(Laik_TransitionPlan* tp,
                               Laik_Mapping* toMap, uint64_t off,
                               int count, int from)
{
    Laik_BackendAction* a = laik_transplan_appendAction(tp);
    a->type = LAIK_AT_Recv;
    a->map = toMap;
    a->offset = off;
    a->toBuf = 0; // not used
    a->count = count;
    a->peer_rank = from;

    tp->recvCount += count;
}

void laik_transplan_recordPackAndSend(Laik_TransitionPlan* tp,
                                      Laik_Mapping* fromMap, Laik_Slice* slc, int to)
{
    Laik_BackendAction* a = laik_transplan_appendAction(tp);
    a->type = LAIK_AT_PackAndSend;
    a->map = fromMap;
    a->slc = slc;
    a->peer_rank = to;

    a->count = laik_slice_size(tp->transition->space->dims, slc);
    assert(a->count > 0);
    tp->sendCount += a->count;
}

void laik_transplan_recordRecvAndUnpack(Laik_TransitionPlan* tp,
                                        Laik_Mapping* toMap, Laik_Slice* slc, int from)
{
    Laik_BackendAction* a = laik_transplan_appendAction(tp);
    a->type = LAIK_AT_RecvAndUnpack;
    a->map = toMap;
    a->slc = slc;
    a->peer_rank = from;

    a->count = laik_slice_size(tp->transition->space->dims, slc);
    assert(a->count > 0);
    tp->recvCount += a->count;
}

void laik_transplan_recordReduce(Laik_TransitionPlan* tp,
                                 char* fromBuf, char* toBuf, int count,
                                 int rootTask, Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_transplan_appendAction(tp);
    a->type = LAIK_AT_Reduce;
    a->fromBuf = fromBuf;
    a->toBuf = toBuf;
    a->count = count;
    a->peer_rank = rootTask;
    a->redOp = redOp;

    assert(a->count > 0);
    tp->reduceCount += a->count;
}

void laik_transplan_recordGroupReduce(Laik_TransitionPlan* tp,
                                      int inputGroup, int outputGroup,
                                      char* fromBuf, char* toBuf, int count,
                                      Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_transplan_appendAction(tp);
    a->type = LAIK_AT_GroupReduce;
    a->inputGroup = inputGroup;
    a->outputGroup = outputGroup;
    a->fromBuf = fromBuf;
    a->toBuf = toBuf;
    a->count = count;
    a->redOp = redOp;

    assert(a->count > 0);
    tp->reduceCount += a->count;
}



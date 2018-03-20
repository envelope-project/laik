/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2018 Josef Weidendorfer
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

    as->bufCount = 0;
    as->bufAllocCount = 0;
    as->buf = 0;

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

    if (as->buf) {
        for(int i = 0; i < as->bufCount; i++)
            free(as->buf[i]);
        free(as->buf);
    }

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

    return a;
}

// allocates buffer and appends it list of buffers used for <tp>, returns off
int laik_actions_addBuf(Laik_ActionSeq* as, int size)
{
    if (as->bufCount == as->bufAllocCount) {
        // enlarge buffer
        as->bufAllocCount = (as->bufCount + 20) * 2;
        as->buf = realloc(as->buf, as->bufAllocCount * sizeof(char**));
        if (!as->buf) {
            laik_panic("Out of memory allocating memory for Laik_TransitionPlan");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    char* buf = malloc(size);
    if (buf) {
        laik_panic("Out of memory allocating memory for Laik_TransitionPlan");
        exit(1); // not actually needed, laik_panic never returns
    }
    int bufNo = as->bufCount;
    as->buf[bufNo] = buf;
    as->bufCount++;

    return bufNo;
}

int laik_actions_addTContext(Laik_ActionSeq* as,
                             Laik_Data* data, Laik_Transition* transition,
                             Laik_MappingList* fromList,
                             Laik_MappingList* toList)
{
    Laik_TransitionContext* tc = malloc(sizeof(Laik_TransitionContext));
    tc->data = data;
    tc->transition = transition;
    tc->fromList = fromList;
    tc->toList = toList;

    assert(as->context[0] == 0);
    as->context[0] = tc;

    return 0;
}


void laik_actions_addSend(Laik_ActionSeq* as,
                               Laik_Mapping* fromMap, uint64_t off,
                               int count, int to)
{
    Laik_BackendAction* a = laik_actions_addAction(as);
    a->type = LAIK_AT_Send;
    a->map = fromMap;
    a->offset = off;
    a->fromBuf = 0; // not used
    a->count = count;
    a->peer_rank = to;

    as->sendCount += count;
}

void laik_actions_addRecv(Laik_ActionSeq* as,
                               Laik_Mapping* toMap, uint64_t off,
                               int count, int from)
{
    Laik_BackendAction* a = laik_actions_addAction(as);
    a->type = LAIK_AT_Recv;
    a->map = toMap;
    a->offset = off;
    a->toBuf = 0; // not used
    a->count = count;
    a->peer_rank = from;

    as->recvCount += count;
}

void laik_actions_addPackAndSend(Laik_ActionSeq* as,
                                      Laik_Mapping* fromMap, Laik_Slice* slc, int to)
{
    Laik_BackendAction* a = laik_actions_addAction(as);
    a->type = LAIK_AT_PackAndSend;
    a->map = fromMap;
    a->slc = slc;
    a->peer_rank = to;

    Laik_TransitionContext* tc = as->context[0];
    a->count = laik_slice_size(tc->transition->space->dims, slc);
    assert(a->count > 0);
    as->sendCount += a->count;
}

void laik_actions_addRecvAndUnpack(Laik_ActionSeq* as,
                                        Laik_Mapping* toMap, Laik_Slice* slc, int from)
{
    Laik_BackendAction* a = laik_actions_addAction(as);
    a->type = LAIK_AT_RecvAndUnpack;
    a->map = toMap;
    a->slc = slc;
    a->peer_rank = from;

    Laik_TransitionContext* tc = as->context[0];
    a->count = laik_slice_size(tc->transition->space->dims, slc);
    assert(a->count > 0);
    as->recvCount += a->count;
}

void laik_actions_addReduce(Laik_ActionSeq* as,
                                 char* fromBuf, char* toBuf, int count,
                                 int rootTask, Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_actions_addAction(as);
    a->type = LAIK_AT_Reduce;
    a->fromBuf = fromBuf;
    a->toBuf = toBuf;
    a->count = count;
    a->peer_rank = rootTask;
    a->redOp = redOp;

    assert(a->count > 0);
    as->reduceCount += a->count;
}

void laik_actions_addGroupReduce(Laik_ActionSeq* as,
                                      int inputGroup, int outputGroup,
                                      char* fromBuf, char* toBuf, int count,
                                      Laik_ReductionOperation redOp)
{
    Laik_BackendAction* a = laik_actions_addAction(as);
    a->type = LAIK_AT_GroupReduce;
    a->inputGroup = inputGroup;
    a->outputGroup = outputGroup;
    a->fromBuf = fromBuf;
    a->toBuf = toBuf;
    a->count = count;
    a->redOp = redOp;

    assert(a->count > 0);
    as->reduceCount += a->count;
}



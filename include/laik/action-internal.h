/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2018 Josef Weidendorfer
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LAIK_ACTION_INTERNAL_H_
#define _LAIK_ACTION_INTERNAL_H_

#include <laik.h>         // for Laik_Instance, Laik_Group, Laik_AccessPhase


// TODO: split off into different action types with minimal space requirements
typedef struct _Laik_BackendAction {
    char type;
    char len;

    // transition context ID
    char tid;

    int count;         // for Send, Recv, Copy, Reduce

    // if 0, fromBuf/toBuf is used
    Laik_Mapping* map; // for Send, Recv, Pack, Unpack, PackAndSend, RecvAndUnpack
    uint64_t offset;   // for Send, Recv

    char* fromBuf;     // for Send, Pack, Copy, Reduce
    char* toBuf;       // for Recv, Unpack, Copy, Reduce
    int peer_rank;     // for Send, Recv, PackAndSend, RecvAndUnpack, Reduce

    // points to slice given in operation of transition
    Laik_Slice* slc;   // for Pack, Unpack, PackAndSend, RecvAndUnpack

    // subgroup IDs defined in transition
    int inputGroup, outputGroup;      // for Reduce
    Laik_ReductionOperation redOp; // for Reduce

} Laik_BackendAction;

typedef struct _Laik_TransitionContext {
    Laik_Transition* transition;
    Laik_Data* data;
} Laik_TransitionContext;

// TODO: Rename to ActionSeq
struct _Laik_TransitionPlan {
    // actions can refer to different transition contexts
#define CONTEXTS_MAX 1
    void* context[CONTEXTS_MAX];

    // allocations done for this plan
    int bufCount, bufAllocCount;
    char** buf;

    // action sequence to trigger on execution
    int actionCount, actionAllocCount;
    Laik_BackendAction* action;

    // summary to update statistics
    int sendCount, recvCount, reduceCount;
};

Laik_TransitionPlan* laik_transplan_new(Laik_Data* d, Laik_Transition* t);
void laik_transplan_free(Laik_TransitionPlan* tp);

// append an invalid backend action
Laik_BackendAction* laik_transplan_appendAction(Laik_TransitionPlan* tp);
// allocate buffer space to use in actions. Returns buffer ID
int laik_transplan_appendBuf(Laik_TransitionPlan* tp, int size);

// append specific actions
void laik_transplan_recordSend(Laik_TransitionPlan* tp,
                               Laik_Mapping* fromMap, uint64_t off,
                               int count, int to);
void laik_transplan_recordRecv(Laik_TransitionPlan* tp,
                               Laik_Mapping* toMap, uint64_t off,
                               int count, int from);
void laik_transplan_recordPackAndSend(Laik_TransitionPlan* tp,
                                      Laik_Mapping* fromMap,
                                      Laik_Slice* slc, int to);
void laik_transplan_recordRecvAndUnpack(Laik_TransitionPlan* tp,
                                        Laik_Mapping* toMap,
                                        Laik_Slice* slc, int from);
void laik_transplan_recordReduce(Laik_TransitionPlan* tp,
                                 char* fromBuf, char* toBuf, int count,
                                 int rootTask, Laik_ReductionOperation redOp);
void laik_transplan_recordGroupReduce(Laik_TransitionPlan* tp,
                                      int inputGroup, int outputGroup,
                                      char* fromBuf, char* toBuf, int count,
                                      Laik_ReductionOperation redOp);

#endif // _LAIK_ACTION_INTERNAL_H_

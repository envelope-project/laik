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


// for CopyFromBuf / CopyToBuf
typedef struct _Laik_CopyEntry {
    char* ptr;
    int offset, bytes;
} Laik_CopyEntry;

// TODO: split off into different action types with minimal space requirements
typedef struct _Laik_BackendAction {
    char type;
    char len;

    // transition context ID
    char tid;

    int count;         // for Send, Recv, Copy, Reduce

    Laik_Mapping* map; // for Pack, Unpack, PackAndSend, RecvAndUnpack
    int mapNo;         // for Send, Recv
    uint64_t offset;   // for Send, Recv

    char* fromBuf;     // for SendBuf, Pack, Copy, Reduce
    char* toBuf;       // for RecvBuf, Unpack, Copy, Reduce
    int peer_rank;     // for Send, Recv, PackAndSend, RecvAndUnpack, Reduce
    Laik_CopyEntry* ce; // for CopyFromBuf, CopyToBuf

    // points to slice given in operation of transition
    Laik_Slice* slc;   // for Pack, Unpack, PackAndSend, RecvAndUnpack

    // subgroup IDs defined in transition
    int inputGroup, outputGroup;      // for Reduce
    Laik_ReductionOperation redOp; // for Reduce

} Laik_BackendAction;


struct _Laik_TransitionContext {
    Laik_Transition* transition;
    Laik_Data* data;
    Laik_MappingList *fromList;
    Laik_MappingList *toList;
};

struct _Laik_ActionSeq {
    Laik_Instance* inst;

    // actions can refer to different transition contexts
#define CONTEXTS_MAX 1
    void* context[CONTEXTS_MAX];

    // buffer space
    char* buf;

    // for copy actions
    Laik_CopyEntry* ce;

    // action sequence to trigger on execution
    int actionCount, actionAllocCount;
    Laik_BackendAction* action;

    // summary to update statistics
    int sendCount, recvCount, reduceCount;
};

Laik_ActionSeq* laik_actions_new(Laik_Instance* inst);
void laik_actions_free(Laik_ActionSeq* as);

// append an invalid backend action
Laik_BackendAction* laik_actions_addAction(Laik_ActionSeq* as);
// allocate buffer space to use in actions. Returns buffer ID
int laik_actions_addBuf(Laik_ActionSeq* as, int size);
// returns the transaction ID
int laik_actions_addTContext(Laik_ActionSeq* as,
                             Laik_Data* d, Laik_Transition* transition,
                             Laik_MappingList* fromList,
                             Laik_MappingList* toList);

// append specific actions
void laik_actions_addSend(Laik_ActionSeq* as,
                          int fromMapNo, uint64_t off,
                          int count, int to);
void laik_actions_addSendBuf(Laik_ActionSeq* as,
                             char* fromBuf, int count, int to);
void laik_actions_addRecv(Laik_ActionSeq* as,
                          int toMapNo, uint64_t off,
                          int count, int from);
void laik_actions_addRecvBuf(Laik_ActionSeq* as,
                             char* toBuf, int count, int from);
void laik_actions_addPackAndSend(Laik_ActionSeq* as,
                                 Laik_Mapping* fromMap,
                                 Laik_Slice* slc, int to);
void laik_actions_addRecvAndUnpack(Laik_ActionSeq* as,
                                   Laik_Mapping* toMap,
                                   Laik_Slice* slc, int from);
void laik_actions_addReduce(Laik_ActionSeq* as,
                            char* fromBuf, char* toBuf, int count,
                            int rootTask, Laik_ReductionOperation redOp);
void laik_actions_addGroupReduce(Laik_ActionSeq* as,
                                 int inputGroup, int outputGroup,
                                 char* fromBuf, char* toBuf, int count,
                                 Laik_ReductionOperation redOp);
void laik_actions_addCopyToBuf(Laik_ActionSeq* as,
                               Laik_CopyEntry* ce, char* toBuf, int count);
void laik_actions_addCopyFromBuf(Laik_ActionSeq* as,
                                 Laik_CopyEntry* ce, char* fromBuf, int count);


// returns a new empty action sequence with same transition context
Laik_ActionSeq* laik_actions_cloneSeq(Laik_ActionSeq* oldAS);

// just copy actions from oldAS into as
void laik_actions_copySeq(Laik_ActionSeq* oldAS, Laik_ActionSeq* as);

// merge send/recv actions from oldAS into as
void laik_actions_optSeq(Laik_ActionSeq* oldAS, Laik_ActionSeq* as);


#endif // _LAIK_ACTION_INTERNAL_H_

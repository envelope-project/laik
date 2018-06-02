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

    // for marking processed actions (used for combining optimization)
    char mark;

    int round;         // order specification into rounds
    int count;         // for Send, Recv, Copy, Reduce
    int bufID;         // for BufReserve, RBufSend, RBufRecv
    Laik_Type* dtype;  // for RBufReduce, BufInit

    Laik_Mapping* map; // for Pack, Unpack, PackAndSend, RecvAndUnpack
    int fromMapNo;     // for MapSend, MapGroupReduce
    int toMapNo;       // for MapRecv, MapGroupReduce
    uint64_t offset;   // for MapSend, MapRecv, RBufSend, RBufRecv

    char* fromBuf;     // for SendBuf, Pack, Copy, Reduce
    char* toBuf;       // for RecvBuf, Unpack, Copy, Reduce
    int peer_rank;     // for Send, Recv, PackAndSend, RecvAndUnpack, Reduce
    Laik_CopyEntry* ce; // for CopyFromBuf, CopyToBuf

    // points to slice given in operation of transition
    int dims;          // for Pack, Unpack, PackAndSend, RecvAndUnpack
    Laik_Slice* slc;   // for Pack, Unpack, PackAndSend, RecvAndUnpack

    // subgroup IDs defined in transition
    int inputGroup, outputGroup;   // for GroupReduce
    Laik_ReductionOperation redOp; // for GroupReduce, Reduce

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
#define ASEQ_CONTEXTS_MAX 1
    void* context[ASEQ_CONTEXTS_MAX];

    // each call to laik_actions_allocBuffer() allocates another buffer
#define ASEQ_BUFFER_MAX 5
    char* buf[ASEQ_BUFFER_MAX];
    int bufSize[ASEQ_BUFFER_MAX];
    int currentBuf;
    int bufReserveCount; // current number of BufReserve actions

    // for copy actions
    Laik_CopyEntry* ce;

    // action sequence to trigger on execution
    int actionCount, actionAllocCount;
    Laik_BackendAction* action;

    // summary to update statistics
    int sendCount, recvCount, reduceCount;
};

// append an invalid action of given size
Laik_Action* laik_aseq_addAction(Laik_ActionSeq* as, int size);

// append an invalid backend action
Laik_BackendAction* laik_aseq_addBAction(Laik_ActionSeq* as);


// initialize transition context
void laik_aseq_initTContext(Laik_TransitionContext* tc,
                            Laik_Data* data, Laik_Transition* transition,
                            Laik_MappingList* fromList,
                            Laik_MappingList* toList);

// returns the transaction ID
int laik_aseq_addTContext(Laik_ActionSeq* as,
                          Laik_Data* d, Laik_Transition* transition,
                          Laik_MappingList* fromList,
                          Laik_MappingList* toList);

// initialize actions
void laik_aseq_initReduce(Laik_BackendAction* a,
                          char* fromBuf, char* toBuf, int count,
                          int rootTask, Laik_ReductionOperation redOp);

void laik_aseq_initGroupReduce(Laik_BackendAction* a,
                               int inputGroup, int outputGroup,
                               char* fromBuf, char* toBuf, int count,
                               Laik_ReductionOperation redOp);

void laik_aseq_initPackAndSend(Laik_BackendAction* a, int round,
                               Laik_Mapping* fromMap, int dims, Laik_Slice* slc,
                               int to);

void laik_aseq_initRecvAndUnpack(Laik_BackendAction* a, int round,
                                 Laik_Mapping* toMap, int dims, Laik_Slice* slc,
                                 int from);

// append action to reserve buffer space, return bufID
int laik_aseq_addBufReserve(Laik_ActionSeq* as, int size, int bufID);

// append send action to buffer referencing a previous reserve action
void laik_aseq_addRBufSend(Laik_ActionSeq* as,
                           int round, int bufID, int byteOffset,
                           int count, int to);

// append recv action into buffer referencing a previous reserve action
void laik_aseq_addRBufRecv(Laik_ActionSeq* as,
                           int round, int bufID, int byteOffset,
                           int count, int from);

// append send action from a mapping with offset
void laik_aseq_addMapSend(Laik_ActionSeq* as, int round,
                          int fromMapNo, uint64_t off,
                          int count, int to);

// append send action from a buffer
void laik_aseq_addBufSend(Laik_ActionSeq* as, int round,
                          char* fromBuf, int count, int to);

// append recv action into a mapping with offset
void laik_aseq_addMapRecv(Laik_ActionSeq* as, int round,
                          int toMapNo, uint64_t off,
                          int count, int from);

// append recv action into a buffer
void laik_aseq_addBufRecv(Laik_ActionSeq* as, int round,
                          char* toBuf, int count, int from);

// append action to call a local reduce operation
void laik_aseq_addRBufLocalReduce(Laik_ActionSeq* as,
                                  int round, Laik_Type *dtype,
                                  Laik_ReductionOperation redOp,
                                  int fromBufID, int fromByteOffset,
                                  char* toBuf, int count);

// append action to call a init operation
void laik_aseq_addBufInit(Laik_ActionSeq* as,
                          int round, Laik_Type *dtype,
                          Laik_ReductionOperation redOp,
                          char* toBuf, int count);

// append action to call a copy operation from/to a buffer
void laik_aseq_addBufCopy(Laik_ActionSeq* as,
                          int round, char* fromBuf, char* toBuf, int count);

// append action to call a copy operation from/to a buffer
void laik_aseq_addRBufCopy(Laik_ActionSeq* as, int round,
                           int fromBufID, int fromByteOffset,
                           char* toBuf, int count);

// append action to pack a slice of data into a buffer
void laik_aseq_addPackToBuf(Laik_ActionSeq* as, int round,
                            Laik_Mapping* fromMap, Laik_Slice* slc, char* toBuf);

// append action to pack a slice of data into a buffer
void laik_aseq_addPackToRBuf(Laik_ActionSeq* as, int round,
                             Laik_Mapping* fromMap, Laik_Slice* slc,
                             int toBufID, int toByteOffset);

// append action to pack a slice of data into a buffer
void laik_aseq_addMapPackToRBuf(Laik_ActionSeq* as, int round,
                                int fromMapNo, Laik_Slice* slc,
                                int toBufID, int toByteOffset);

// append action to pack a slice of data into temp buffer and send it
void laik_aseq_addMapPackAndSend(Laik_ActionSeq* as, int round,
                                 int fromMapNo, Laik_Slice* slc, int to);

// append action to pack a slice of data into temp buffer and send it
void laik_aseq_addPackAndSend(Laik_ActionSeq* as, int round,
                              Laik_Mapping* fromMap,
                              Laik_Slice* slc, int to);

// append action to unpack data from buffer into a slice of data
void laik_aseq_addUnpackFromBuf(Laik_ActionSeq* as, int round,
                                char* fromBuf, Laik_Mapping* toMap, Laik_Slice* slc);

// append action to unpack data from buffer into a slice of data
void laik_aseq_addUnpackFromRBuf(Laik_ActionSeq* as, int round,
                                 int fromBufID, int fromByteOffset,
                                 Laik_Mapping* toMap, Laik_Slice* slc);

// append action to unpack data from buffer into a slice of data
void laik_aseq_addMapUnpackFromRBuf(Laik_ActionSeq* as, int round,
                                    int fromBufID, int fromByteOffset,
                                    int toMapNo, Laik_Slice* slc);

// append action to receive data into temp buffer and unpack it into a slice of data
void laik_aseq_addMapRecvAndUnpack(Laik_ActionSeq* as, int round,
                                   int toMapNo, Laik_Slice* slc, int from);

// append action to receive data into temp buffer and unpack it into a slice of data
void laik_aseq_addRecvAndUnpack(Laik_ActionSeq* as, int round,
                                Laik_Mapping* toMap,
                                Laik_Slice* slc, int from);

// append action to reduce data in buffer from all to buffer in rootTask
void laik_aseq_addReduce(Laik_ActionSeq* as,
                         char* fromBuf, char* toBuf, int count,
                         int rootTask, Laik_ReductionOperation redOp);

// append action to reduce data in buffer from inputGroup to buffer in outputGroup
void laik_aseq_addGroupReduce(Laik_ActionSeq* as,
                              int inputGroup, int outputGroup,
                              char* fromBuf, char* toBuf, int count,
                              Laik_ReductionOperation redOp);

// append action to gather a sequence of arrays into one packed buffer
void laik_aseq_addCopyToBuf(Laik_ActionSeq* as, int round,
                            Laik_CopyEntry* ce, char* toBuf, int count);

// append action to scather packed arrays in one buffer to multiple buffers
void laik_aseq_addCopyFromBuf(Laik_ActionSeq* as, int round,
                              Laik_CopyEntry* ce, char* fromBuf, int count);

// append action to reduce data in buffer from inputGroup to same buffer in outputGroup
// the buffer is specified by a reserve buffer ID and an offset
void laik_aseq_addRBufGroupReduce(Laik_ActionSeq* as,
                                  int inputGroup, int outputGroup,
                                  int bufID, int byteOffset, int count,
                                  Laik_ReductionOperation redOp);

// append action to gather a sequence of arrays into one packed buffer
// the buffer is specified by a reserve buffer ID and an offset
void laik_aseq_addCopyToRBuf(Laik_ActionSeq* as, int round,
                             Laik_CopyEntry* ce,
                             int toBufID, int toByteOffset, int count);

// append action to scather packed arrays in one buffer to multiple buffers
// the buffer is specified by a reserve buffer ID and an offset
void laik_aseq_addCopyFromRBuf(Laik_ActionSeq* as, int round,
                               Laik_CopyEntry* ce,
                               int fromBufID, int fromByteOffset, int count);

// add all reduce ops from a transition to an ActionSeq.
void laik_aseq_addReds(Laik_ActionSeq* as,
                       Laik_Data* data, Laik_Transition* t);

// add all receive ops from a transition to an ActionSeq
void laik_aseq_addRecvs(Laik_ActionSeq* as, int round,
                        Laik_Data* data, Laik_Transition* t);


// add all send ops from a transition to an ActionSeq
void laik_aseq_addSends(Laik_ActionSeq* as, int round,
                        Laik_Data* data, Laik_Transition* t);

// collect buffer reservation actions and update actions referencing them
// works in-place, only call once
void laik_aseq_allocBuffer(Laik_ActionSeq* as);


//
// generic transformation passes for action sequences
// (called by backends)

// returns a new empty action sequence with same transition context
Laik_ActionSeq* laik_actions_setupTransform(Laik_ActionSeq* oldAS);

// append action <ba> to <as>
void laik_actions_add(Laik_BackendAction* ba, Laik_ActionSeq* as);


// just copy actions from oldAS into as
void laik_aseq_copySeq(Laik_ActionSeq* oldAS, Laik_ActionSeq* as);

// merge send/recv actions from oldAS into as
void laik_aseq_combineActions(Laik_ActionSeq* oldAS, Laik_ActionSeq* as);

// add sorted send/recv actions from as into as2 to avoid deadlocks
void laik_aseq_sort_2phases(Laik_ActionSeq* as, Laik_ActionSeq *as2);
void laik_aseq_sort_rankdigits(Laik_ActionSeq* as, Laik_ActionSeq* as2);

// transform MapPackAndSend/MapRecvAndUnpack into simple Send/Recv actions
void laik_aseq_flattenPacking(Laik_ActionSeq* as, Laik_ActionSeq* as2);

// transformation for split reduce actions into basic multiple actions
void laik_aseq_splitReduce(Laik_ActionSeq* as, Laik_ActionSeq* as2);

#endif // _LAIK_ACTION_INTERNAL_H_

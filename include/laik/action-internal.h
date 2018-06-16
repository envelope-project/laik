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

// Action sequences are used in the public LAIK API as abstraction
// for compound communication requests, e.g. consisting of multiple
// container transitions. Users can build such sequences and trigger
// their execution, but the data structures and actions within sequences
// are hidden to LAIK users.
//
// Action sequences and actions are used in the interface to backends
// to (1) specify what compound communication should be executed, and
// (2) to allow backends to do transformations and internal resource
// allocations before actual execution of an action sequence, to optimize
// their execution. For (2), a backend can define its own custom action
// types, and do transformations which add such back-end specific actions
// for faster execution afterwards.
//
// In this file, backend-independent actions and transformation
// funactions are defined. They may be useful by backends for optimizing
// a sequence.

// all actions must start with this 4-byte header
struct _Laik_Action {
    unsigned char type;
    unsigned char len;
    unsigned char round;   // actions are order by rounds
    unsigned char tid  :7; // ID of transition context for this action
    unsigned char mark :1; // boolean flag used in some transformations
};

// for iterating action sequences
#define nextAction(a) ((Laik_Action*) (((char*)a) + a->len))


// backend-independent action structs:
// only requried if further parameters need to be stored, ie. not for
//   Nop, Halt, TExec, ...

// BufReserve action
typedef struct {
    Laik_Action h;
    int size;  // in bytes
    int bufID;
    int offset;
} Laik_A_BufReserve;

// RBufSend action
typedef struct {
    Laik_Action h;
    int bufID;
    int offset;
    int count;
    int to_rank;
} Laik_A_RBufSend;

// RBufRecv action
typedef struct {
    Laik_Action h;
    int bufID;
    int offset;
    int count;
    int from_rank;
} Laik_A_RBufRecv;

// BufSend action
typedef struct {
    Laik_Action h;
    int count;
    int to_rank;
    char* buf;
} Laik_A_BufSend;

// BufRecv action
typedef struct {
    Laik_Action h;
    int count;
    int from_rank;
    char* buf;
} Laik_A_BufRecv;


// helper struct for CopyFromBuf / CopyToBuf
typedef struct _Laik_CopyEntry {
    char* ptr;
    int offset, bytes;
} Laik_CopyEntry;

// TODO: split off into different action types with minimal space requirements
typedef struct _Laik_BackendAction {
    // header
    Laik_Action h;

    int count;         // for Send, Recv, Copy, Reduce
    int bufID;         // for BufReserve, RBufSend, RBufRecv
    Laik_Type* dtype;  // for RBufReduce, BufInit

    Laik_Mapping* map; // for Pack, Unpack, PackAndSend, RecvAndUnpack
    int fromMapNo;     // for MapSend, MapGroupReduce
    int toMapNo;       // for MapRecv, MapGroupReduce
    int offset;        // for MapSend, MapRecv, RBufSend, RBufRecv

    char* fromBuf;     // for SendBuf, Pack, Copy, Reduce
    char* toBuf;       // for RecvBuf, Unpack, Copy, Reduce
    int rank;     // for Send, Recv, PackAndSend, RecvAndUnpack, Reduce
    Laik_CopyEntry* ce; // for CopyFromBuf, CopyToBuf

    // points to slice given in operation of transition
    int dims;          // for Pack, Unpack, PackAndSend, RecvAndUnpack
    Laik_Slice* slc;   // for Pack, Unpack, PackAndSend, RecvAndUnpack

    // subgroup IDs defined in transition
    int inputGroup, outputGroup;   // for GroupReduce
    Laik_ReductionOperation redOp; // for GroupReduce, Reduce

} Laik_BackendAction;


// a transition context, referenced in an action sequence
//
// this specifies a concrete transition to be done on a data container
// which uses a given set of memory mappings before/after the transition

struct _Laik_TransitionContext {
    Laik_Transition* transition;
    Laik_Data* data;
    Laik_MappingList *fromList;
    Laik_MappingList *toList;
    // from/to Lists when prepared by backend
    Laik_MappingList *prepFromList;
    Laik_MappingList *prepToList;
};


// An action sequence is a list of actions for a compound communication
// request, to be executed in some order.
//
// Each action belongs to a round; actions within a round have no dependence
// on each other and can be executed asynchronously in parallel. This is
// useful to specify required ordering, e.g. copy actions into a temporary
// buffer before this buffer is sent. But it also enables asynchronity with
// action triggering a communciation in hardware, and other actions waiting
// on completion.
//
// The execution of an action sequence expects actions to be sorted by rounds.
// An execution request to a backend can ask only for some rounds to be
// executed, allowing LAIK to expose an API to users which enables overlap
// of computation and communication.
//
// Once a action sequence is optimized by a given backend, it only can
// be executed by this backend, as it may contain backend-specific actions.
// There are backend-independent actions and transformations provided which
// allow backends to request allocation of temporary buffers, which can be
// used in other actions. LAIK will allocated such buffers for the backend
// before execution, and clean them up when the action sequence is deleted.

struct _Laik_ActionSeq {
    Laik_Instance* inst;

    // if non-null, only this backend can execute the sequence, and
    // the backend gets called for clean-up when the sequence is destroyed
    Laik_Backend* backend;

    // actions can refer to different transition contexts
#define ASEQ_CONTEXTS_MAX 1
    void* context[ASEQ_CONTEXTS_MAX];
    int contextCount;

    // each call to laik_aseq_allocBuffer() allocates another buffer
#define ASEQ_BUFFER_MAX 5
    char* buf[ASEQ_BUFFER_MAX];
    int bufSize[ASEQ_BUFFER_MAX];
    int bufferCount;
    int bufReserveCount; // current number of BufReserve actions

    // for copy actions
#define ASEQ_COPYENTRY_MAX 5
    Laik_CopyEntry* ce[ASEQ_COPYENTRY_MAX];
    int ceCount;
    int ceRanges;

    // action sequence to trigger on execution
    int actionCount, bytesUsed;
    Laik_Action* action;
    // how many rounds
    int roundCount;

    // temporary action sequence storage used during generation by
    // laik_aseq_addAction(). Call laik_aseq_finish to make it active
    // (ie. set <action> array to this temporary seq)
    int newActionCount, newBytesUsed, newBytesAlloc;
    Laik_BackendAction* newAction;
    int newRoundCount;


    // summary to update statistics
    int sendCount, recvCount, reduceCount;
};



// helpers for building new action sequences. New actions are first
// stored in temporary space, and only becoming active when calling
// laik_aseq_activateNewActions(). During build, there may already exist
// an active sequence. Transformation typically travers the active sequence
// and build up a new sequence within the same action sequence object.

// append an invalid action of given size
Laik_Action* laik_aseq_addAction(Laik_ActionSeq* as, int size,
                                 Laik_ActionType type, int round, int tid);

// discard any new built actions (e.g. if there was no change to old seq)
void laik_aseq_discardNewActions(Laik_ActionSeq* as);

// finish building an action sequence, activate the new built sequence
void laik_aseq_activateNewActions(Laik_ActionSeq* as);

// free temporary space used for building a new sequence
void laik_aseq_freeTempSpace(Laik_ActionSeq* as);

// append an invalid backend action
Laik_BackendAction* laik_aseq_addBAction(Laik_ActionSeq* as, int round);


// returns the transaction ID
int laik_aseq_addTContext(Laik_ActionSeq* as,
                          Laik_Data* d, Laik_Transition* transition,
                          Laik_MappingList* fromList,
                          Laik_MappingList* toList);

// append action to stop execution (even if there are more in the sequence)
void laik_aseq_addHalt(Laik_ActionSeq* as);

// append action to do the transition specified by the transition context ID
void laik_aseq_addTExec(Laik_ActionSeq* as, int tid);

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

// append action to pack a slice of data into a temp buffer
void laik_aseq_addMapPackToRBuf(Laik_ActionSeq* as, int round,
                                int fromMapNo, Laik_Slice* slc,
                                int toBufID, int toByteOffset);

// append action to pack a slice of data into a buffer
void laik_aseq_addMapPackToBuf(Laik_ActionSeq* as, int round,
                               int fromMapNo, Laik_Slice* slc, char* toBuf);

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

// append action to receive data and unpack into a slice of data
void laik_aseq_addMapRecvAndUnpack(Laik_ActionSeq* as, int round,
                                   int toMapNo, Laik_Slice* slc, int from);

// append action to unpack data from buffer into a slice of data
void laik_aseq_addUnpackFromRBuf(Laik_ActionSeq* as, int round,
                                 int fromBufID, int fromByteOffset,
                                 Laik_Mapping* toMap, Laik_Slice* slc);

// append action to unpack data from temp buffer into a slice of data
void laik_aseq_addMapUnpackFromRBuf(Laik_ActionSeq* as, int round,
                                    int fromBufID, int fromByteOffset,
                                    int toMapNo, Laik_Slice* slc);

// append action to unpack data from buffer into a slice of data
void laik_aseq_addMapUnpackFromBuf(Laik_ActionSeq* as, int round,
                                   char* fromBuf, int toMapNo, Laik_Slice* slc);

// append action to receive data into temp buffer and unpack it into a slice of data
void laik_aseq_addMapRecvAndUnpack(Laik_ActionSeq* as, int round,
                                   int toMapNo, Laik_Slice* slc, int from);

// append action to receive data into temp buffer and unpack it into a slice of data
void laik_aseq_addRecvAndUnpack(Laik_ActionSeq* as, int round,
                                Laik_Mapping* toMap,
                                Laik_Slice* slc, int from);

// append action to reduce data in buffer from all to buffer in rootTask
void laik_aseq_addReduce(Laik_ActionSeq* as, int round,
                         char* fromBuf, char* toBuf, int count,
                         int rootTask, Laik_ReductionOperation redOp);

// append action to reduce data in temp buffer from all to buffer in rootTask
void laik_aseq_addRBufReduce(Laik_ActionSeq* as, int round,
                             int bufID, int byteOffset, int count,
                             int rootTask, Laik_ReductionOperation redOp);

// append action to reduce data in buffer from inputGroup to buffer in outputGroup
void laik_aseq_addGroupReduce(Laik_ActionSeq* as, int round,
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
void laik_aseq_addRBufGroupReduce(Laik_ActionSeq* as, int round,
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
void laik_aseq_addReds(Laik_ActionSeq* as, int round,
                       Laik_Data* data, Laik_Transition* t);

// add all receive ops from a transition to an ActionSeq
void laik_aseq_addRecvs(Laik_ActionSeq* as, int round,
                        Laik_Data* data, Laik_Transition* t);


// add all send ops from a transition to an ActionSeq
void laik_aseq_addSends(Laik_ActionSeq* as, int round,
                        Laik_Data* data, Laik_Transition* t);

// collect buffer reservation actions and update actions referencing them
// works in-place, only call once
bool laik_aseq_allocBuffer(Laik_ActionSeq* as);


//
// generic transformation passes for action sequences
// (called by backends)

// append action <ba> to <as>, change round if not negative
void laik_aseq_add(Laik_Action* a, Laik_ActionSeq* as, int round);


// just copy actions from oldAS into as
void laik_aseq_copySeq(Laik_ActionSeq* as);

// merge send/recv actions from oldAS into as
bool laik_aseq_combineActions(Laik_ActionSeq* as);

// add sorted send/recv actions from as into as2 to avoid deadlocks
bool laik_aseq_sort_2phases(Laik_ActionSeq* as);
bool laik_aseq_sort_rankdigits(Laik_ActionSeq* as);

// sort actions according to their rounds, and compress rounds
bool laik_aseq_sort_rounds(Laik_ActionSeq* as);

// transform MapPackAndSend/MapRecvAndUnpack into simple Send/Recv actions
bool laik_aseq_flattenPacking(Laik_ActionSeq* as);

// transformation for split reduce actions into basic multiple actions
bool laik_aseq_splitReduce(Laik_ActionSeq* as);

// replace group reduction actions with all-reduction actions if possible
bool laik_aseq_replaceWithAllReduce(Laik_ActionSeq* as);

// replace transition exec actions with equivalent reduce/send/recv actions
bool laik_aseq_splitTransitionExecs(Laik_ActionSeq* as);

#endif // _LAIK_ACTION_INTERNAL_H_

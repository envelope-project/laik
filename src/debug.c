/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017, 2018 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
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


/**
 * utility functions for debug output
*/

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


// to be used with buffered log API

void laik_log_IntList(int len, int* list)
{
    laik_log_append("[");
    for(int i = 0; i < len; i++)
        laik_log_append("%s%d", (i>0) ? ", ":"", list[i]);
    laik_log_append("]");
}


void laik_log_Space(Laik_Space* spc)
{
    switch(spc->dims) {
    case 1:
        laik_log_append("[%lld;%lld[",
                        (long long) spc->s.from.i[0],
                        (long long) spc->s.to.i[0] );
        break;
    case 2:
        laik_log_append("[%lld;%lld[ x [%lld;%lld[",
                        (long long) spc->s.from.i[0],
                        (long long) spc->s.to.i[0],
                        (long long) spc->s.from.i[1],
                        (long long) spc->s.to.i[1] );
        break;
    case 3:
        laik_log_append("[%lld;%lld[ x [%lld;%lld[ x [%lld;%lld[",
                        (long long) spc->s.from.i[0],
                        (long long) spc->s.to.i[0],
                        (long long) spc->s.from.i[1],
                        (long long) spc->s.to.i[1],
                        (long long) spc->s.from.i[2],
                        (long long) spc->s.to.i[2] );
        break;
    default: assert(0);
    }
}

void laik_log_Index(int dims, const Laik_Index* idx)
{
    int64_t i1 = idx->i[0];
    int64_t i2 = idx->i[1];
    int64_t i3 = idx->i[2];

    switch(dims) {
    case 1:
        laik_log_append("%lld", (long long) i1);
        break;
    case 2:
        laik_log_append("%lld/%lld",
                        (long long) i1,
                        (long long) i2);
        break;
    case 3:
        laik_log_append("%lld/%lld/%lld",
                        (long long) i1,
                        (long long) i2,
                        (long long) i3);
        break;
    default: assert(0);
    }
}

void laik_log_Slice(Laik_Slice* slc)
{
    if (laik_slice_isEmpty(slc)) {
        laik_log_append("(empty)");
        return;
    }

    int dims = slc->space->dims;
    laik_log_append("[");
    laik_log_Index(dims, &(slc->from));
    laik_log_append(";");
    laik_log_Index(dims, &(slc->to));
    laik_log_append("[");
}

void laik_log_Reduction(Laik_ReductionOperation op)
{
    switch(op) {
    case LAIK_RO_None: laik_log_append("-"); break;
    case LAIK_RO_Sum:  laik_log_append("sum"); break;
    case LAIK_RO_Prod: laik_log_append("prod"); break;
    case LAIK_RO_Min:  laik_log_append("min"); break;
    case LAIK_RO_Max:  laik_log_append("max"); break;
    case LAIK_RO_And:  laik_log_append("bitwise and"); break;
    case LAIK_RO_Or:   laik_log_append("bitwise or"); break;
    default: assert(0);
    }
}

void laik_log_DataFlow(Laik_DataFlow flow)
{
    switch(flow) {
    case LAIK_DF_None:     laik_log_append("-"); break;
    case LAIK_DF_Preserve: laik_log_append("preserve"); break;
    case LAIK_DF_Init:     laik_log_append("init"); break;
    default: assert(0);
    }
}

void laik_log_TransitionGroup(Laik_Transition* t, int group)
{
    if (group == -1) {
        laik_log_append("(all)");
        return;
    }

    assert(group < t->subgroupCount);
    TaskGroup* tg = &(t->subgroup[group]);

    laik_log_append("(");
    for(int i = 0; i < tg->count; i++) {
        if (i > 0) laik_log_append(",");
        laik_log_append("T%d", tg->task[i]);
    }
    laik_log_append(")");
}

void laik_log_Transition(Laik_Transition* t, bool showActions)
{
    if (t->fromPartitioning)
        laik_log_append("'%s'", t->fromPartitioning->name);
    else
        laik_log_append("(-)");
    laik_log_append(" ==(");
    laik_log_DataFlow(t->flow);
    if (laik_is_reduction(t->redOp)) {
        laik_log_append("/");
        laik_log_Reduction(t->redOp);
    }
    laik_log_append(")=> ");
    if (t->toPartitioning)
        laik_log_append("'%s'", t->toPartitioning->name);
    else
        laik_log_append("(-)");
    if (!showActions) return;

    laik_log_append(": ");

    if ((t == 0) ||
        (t->localCount + t->initCount +
         t->sendCount + t->recvCount + t->redCount == 0)) {
        laik_log_append("(no actions)");
        return;
    }

    if (t->localCount>0) {
        laik_log_append("\n   %2d local: ", t->localCount);
        for(int i=0; i<t->localCount; i++) {
            if (i>0) laik_log_append(", ");
            laik_log_Slice(&(t->local[i].slc));
        }
    }

    if (t->initCount>0) {
        laik_log_append("\n   %2d init : ", t->initCount);
        for(int i=0; i<t->initCount; i++) {
            if (i>0) laik_log_append(", ");
            laik_log_Reduction(t->init[i].redOp);
            laik_log_Slice(&(t->init[i].slc));
        }
    }

    if (t->sendCount>0) {
        laik_log_append("\n   %2d send : ", t->sendCount);
        for(int i=0; i<t->sendCount; i++) {
            if (i>0) laik_log_append(", ");
            laik_log_Slice(&(t->send[i].slc));
            laik_log_append("==>T%d", t->send[i].toTask);
        }
    }

    if (t->recvCount>0) {
        laik_log_append("\n   %2d recv : ", t->recvCount);
        for(int i=0; i<t->recvCount; i++) {
            if (i>0) laik_log_append(", ");
            laik_log_append("T%d==>", t->recv[i].fromTask);
            laik_log_Slice(&(t->recv[i].slc));
        }
    }

    if (t->redCount>0) {
        laik_log_append("\n   %2d reduc: ", t->redCount);
        for(int i=0; i<t->redCount; i++) {
            if (i>0) laik_log_append(", ");
            laik_log_Slice(&(t->red[i].slc));
            laik_log_append(" ");
            laik_log_TransitionGroup(t, t->red[i].inputGroup);
            laik_log_append("=(");
            laik_log_Reduction(t->red[i].redOp);
            laik_log_append(")=>");
            laik_log_TransitionGroup(t, t->red[i].outputGroup);
        }
    }
}

void laik_log_Partitioning(Laik_Partitioning* p)
{
    if (!p) {
        laik_log_append("(no partitioning)");
        return;
    }

    assert(p->tslice); // only show generic slices
    laik_log_append("partitioning '%s': %d slices in %d tasks on ",
                    p->name, p->count, p->group->size);
    laik_log_Space(p->space);
    if (p->tfilter >= 0)
        laik_log_append(" (just task %d)", p->tfilter);
    laik_log_append(": (task:slice:tag/mapNo)\n    ");
    for(int i = 0; i < p->count; i++) {
        Laik_TaskSlice_Gen* ts = &(p->tslice[i]);
        if (i>0)
            laik_log_append(", ");
        laik_log_append("%d:", ts->task);
        laik_log_Slice(&(ts->s));
        laik_log_append(":%d/%d", ts->tag, ts->mapNo);
    }
}

void laik_log_PrettyInt(uint64_t v)
{
    double vv = (double) v;
    if (vv > 1000000000.0) {
        laik_log_append("%.1f G", vv / 1000000000.0);
        return;
    }
    if (vv > 1000000.0) {
        laik_log_append("%.1f M", vv / 1000000.0);
        return;
    }
    if (vv > 1000.0) {
        laik_log_append("%.1f K", vv / 1000.0);
        return;
    }
    laik_log_append("%.0f ", vv);
}

void laik_log_SwitchStat(Laik_SwitchStat* ss)
{
    laik_log_append("%d switches (%d without actions)\n",
                    ss->switches, ss->switches_noactions);
    if (ss->switches == ss->switches_noactions) return;

    if (ss->mallocCount > 0) {
        laik_log_append("    malloc: %dx, ", ss->mallocCount);
        laik_log_PrettyInt(ss->mallocedBytes);
        laik_log_append("B, freed: %dx, ", ss->freeCount);
        laik_log_PrettyInt(ss->freedBytes);
        laik_log_append("B, copied ");
        laik_log_PrettyInt(ss->copiedBytes);
        laik_log_append("B\n");
    }
    if ((ss->sendCount > 0) || (ss->recvCount > 0)) {
        laik_log_append("    sent: %dx, ", ss->sendCount);
        laik_log_PrettyInt(ss->sentBytes);
        laik_log_append("B, recv: %dx, ", ss->recvCount);
        laik_log_PrettyInt(ss->receivedBytes);
        laik_log_append("B\n");
    }
    if (ss->reduceCount) {
        laik_log_append("    reduce: %dx, ", ss->reduceCount);
        laik_log_PrettyInt(ss->reducedBytes);
        laik_log_append("B, initialized ");
        laik_log_PrettyInt(ss->initedBytes);
        laik_log_append("B\n");
    }
}

char* laik_at_str(Laik_ActionType t)
{
    switch(t) {
    case LAIK_AT_Invalid:           return "Invalid";
    case LAIK_AT_Nop:               return "Nop";
    case LAIK_AT_TExec:             return "TExec";
    case LAIK_AT_BufReserve:        return "BufReserve";
    case LAIK_AT_MapSend:           return "MapSend";
    case LAIK_AT_BufSend:           return "BufSend";
    case LAIK_AT_RBufSend:          return "RBufSend";
    case LAIK_AT_MapRecv:           return "MapRecv";
    case LAIK_AT_BufRecv:           return "BufRecv";
    case LAIK_AT_RBufRecv:          return "RBufRecv";
    case LAIK_AT_CopyFromBuf:       return "CopyFromBuf";
    case LAIK_AT_CopyToBuf:         return "CopyToBuf";
    case LAIK_AT_CopyFromRBuf:      return "CopyFromRBuf";
    case LAIK_AT_CopyToRBuf:        return "CopyToRBuf";
    case LAIK_AT_BufCopy:           return "BufCopy";
    case LAIK_AT_RBufCopy:          return "RBufCopy";
    case LAIK_AT_Copy:              return "Copy";
    case LAIK_AT_Reduce:            return "Reduce";
    case LAIK_AT_RBufReduce:        return "RBufReduce";
    case LAIK_AT_MapGroupReduce:    return "MapGroupReduce";
    case LAIK_AT_GroupReduce:       return "GroupReduce";
    case LAIK_AT_RBufGroupReduce:   return "RBufGroupReduce";
    case LAIK_AT_RBufLocalReduce:   return "RBufLocalReduce";
    case LAIK_AT_BufInit:           return "BufInit";
    case LAIK_AT_PackToBuf:         return "PackToBuf";
    case LAIK_AT_PackToRBuf:        return "PackToRBuf";
    case LAIK_AT_MapPackToRBuf:     return "MapPackToRBuf";
    case LAIK_AT_MapPackToBuf:      return "MapPackToBuf";
    case LAIK_AT_MapPackAndSend:    return "MapPackAndSend";
    case LAIK_AT_PackAndSend:       return "PackAndSend";
    case LAIK_AT_UnpackFromBuf:     return "UnpackFromBuf";
    case LAIK_AT_UnpackFromRBuf:    return "UnpackFromRBuf";
    case LAIK_AT_MapUnpackFromRBuf: return "MapUnpackFromRBuf";
    case LAIK_AT_MapUnpackFromBuf:  return "MapUnpackFromBuf";
    case LAIK_AT_RecvAndUnpack:     return "RecvAndUnpack";
    case LAIK_AT_MapRecvAndUnpack:  return "MapRecvAndUnpack";
    default: break;
    }
    return "???";
}

void laik_log_Action(Laik_Action* a, Laik_ActionSeq* as)
{
    Laik_TransitionContext* tc = as->context[a->tid];
    laik_log_append("  %4d %s (R %d, tid %d)",
                    ((char*)a) - ((char*)(as->action)),
                    laik_at_str(a->type), a->round, a->tid);

    Laik_BackendAction* ba = (Laik_BackendAction*) a;
    switch(a->type) {
    case LAIK_AT_Nop:
    case LAIK_AT_TExec:
        break;

    case LAIK_AT_BufReserve: {
        Laik_A_BufReserve* aa = (Laik_A_BufReserve*) a;
        laik_log_append(": buf id %d, size %d", aa->bufID, aa->size);
        break;
    }

    case LAIK_AT_MapSend:
        laik_log_append(": from mapNo %d, off %d, count %d ==> T%d",
                        ba->fromMapNo,
                        ba->offset,
                        ba->count,
                        ba->rank);
        break;

    case LAIK_AT_BufSend: {
        Laik_A_BufSend* aa = (Laik_A_BufSend*) a;
        laik_log_append(": from %p, count %d ==> T%d",
                        aa->buf,
                        aa->count,
                        aa->to_rank);
        break;
    }

    case LAIK_AT_RBufSend: {
        Laik_A_RBufSend* aa = (Laik_A_RBufSend*) a;
        laik_log_append(": from buf %d, off %lld, count %d ==> T%d",
                        aa->bufID, (long long int) aa->offset,
                        aa->count,
                        aa->to_rank);
        break;
    }

    case LAIK_AT_MapRecv:
        laik_log_append(": T%d ==> to mapNo %d, off %lld, count %d",
                        ba->rank,
                        ba->toMapNo,
                        (long long int) ba->offset,
                        ba->count);
        break;

    case LAIK_AT_BufRecv: {
        Laik_A_BufRecv* aa = (Laik_A_BufRecv*) a;
        laik_log_append(": T%d ==> to %p, count %d",
                        aa->from_rank,
                        aa->buf,
                        aa->count);
        break;
    }

    case LAIK_AT_RBufRecv: {
        Laik_A_RBufRecv* aa = (Laik_A_RBufRecv*) a;
        laik_log_append(": T%d ==> to buf %d, off %lld, count %d",
                        aa->from_rank,
                        aa->bufID, (long long int) aa->offset,
                        aa->count);
        break;
    }

    case LAIK_AT_CopyFromBuf:
        laik_log_append(": buf %p, ranges %d",
                        ba->fromBuf,
                        ba->count);
        for(int i = 0; i < ba->count; i++)
            laik_log_append("\n        off %d, bytes %d => to %p",
                            ba->ce[i].offset,
                            ba->ce[i].bytes,
                            ba->ce[i].ptr);
        break;

    case LAIK_AT_CopyToBuf:
        laik_log_append(": buf %p, ranges %d",
                        ba->toBuf,
                        ba->count);
        for(int i = 0; i < ba->count; i++)
            laik_log_append("\n        %p => off %d, bytes %d",
                            ba->ce[i].ptr,
                            ba->ce[i].offset,
                            ba->ce[i].bytes);
        break;

    case LAIK_AT_CopyFromRBuf:
        laik_log_append(": buf %d, off %lld, ranges %d",
                        ba->bufID, (long long int) ba->offset,
                        ba->count);
        for(int i = 0; i < ba->count; i++)
            laik_log_append("\n        off %d, bytes %d => to %p",
                            ba->ce[i].offset,
                            ba->ce[i].bytes,
                            ba->ce[i].ptr);
        break;

    case LAIK_AT_CopyToRBuf:
        laik_log_append(": buf %d, off %lld, ranges %d",
                        ba->bufID, (long long int) ba->offset,
                        ba->count);
        for(int i = 0; i < ba->count; i++)
            laik_log_append("\n        %p => off %d, bytes %d",
                            ba->ce[i].ptr,
                            ba->ce[i].offset,
                            ba->ce[i].bytes);
        break;

    case LAIK_AT_BufCopy:
        laik_log_append(": from %p, to %p, count %d",
                        ba->fromBuf,
                        ba->toBuf,
                        ba->count);
        break;

    case LAIK_AT_RBufCopy:
        laik_log_append(": from buf %d off %lld, to %p, count %d",
                        ba->bufID, (long long int) ba->offset,
                        (void*) ba->toBuf,
                        ba->count);
        break;

    case LAIK_AT_Copy:
        laik_log_append(": count %d", ba->count);
        break;

    case LAIK_AT_Reduce:
        laik_log_append(": count %d, from %p, to %p, root ",
                        ba->count,
                        (void*) ba->fromBuf, (void*) ba->toBuf);
        if (ba->rank == -1)
            laik_log_append("(all)");
        else
            laik_log_append("%d", ba->rank);
        break;

    case LAIK_AT_RBufReduce:
        laik_log_append(": count %d, from/to buf %d off %lld, root ",
                        ba->count, ba->bufID, ba->offset);
        if (ba->rank == -1)
            laik_log_append("(all)");
        else
            laik_log_append("%d", ba->rank);
        break;

    case LAIK_AT_MapGroupReduce:
        laik_log_append(": ");
        laik_log_Slice(ba->slc);
        laik_log_append(" myInMapNo %d, myOutMapNo %d, count %d, input ",
                        ba->fromMapNo, ba->toMapNo, ba->count);
        laik_log_TransitionGroup(tc->transition, ba->inputGroup);
        laik_log_append(", output ");
        laik_log_TransitionGroup(tc->transition, ba->outputGroup);
        break;

    case LAIK_AT_GroupReduce:
        laik_log_append(": count %d, from %p, to %p, input ",
                        ba->count,
                        (void*) ba->fromBuf, (void*) ba->toBuf);
        laik_log_TransitionGroup(tc->transition, ba->inputGroup);
        laik_log_append(", output ");
        laik_log_TransitionGroup(tc->transition, ba->outputGroup);
        break;

    case LAIK_AT_RBufGroupReduce:
        laik_log_append(": count %d, from/to buf %d, off %lld, input ",
                        ba->count,
                        ba->bufID, (long long int) ba->offset);
        laik_log_TransitionGroup(tc->transition, ba->inputGroup);
        laik_log_append(", output ");
        laik_log_TransitionGroup(tc->transition, ba->outputGroup);
        break;

    case LAIK_AT_RBufLocalReduce:
        laik_log_append(": type %s, redOp ", ba->dtype->name);
        laik_log_Reduction(ba->redOp);
        laik_log_append(", from buf %d off %lld, to %p, count %d",
                        ba->bufID, (long long int) ba->offset,
                        ba->toBuf, ba->count);
        break;

    case LAIK_AT_BufInit:
        laik_log_append(": type %s, redOp ", ba->dtype->name);
        laik_log_Reduction(ba->redOp);
        laik_log_append(", to %p, count %d",
                        (void*) ba->toBuf, ba->count);
        break;

    case LAIK_AT_PackToBuf:
        laik_log_append(": ");
        laik_log_Slice(ba->slc);
        laik_log_append(" count %d ==> buf %p",
                        ba->count, (void*) ba->toBuf);
        break;

    case LAIK_AT_PackToRBuf:
        laik_log_append(": ");
        laik_log_Slice(ba->slc);
        laik_log_append(" count %d ==> buf %d off %lld",
                        ba->count, ba->bufID, ba->offset);
        break;

    case LAIK_AT_MapPackToRBuf:
        laik_log_append(": ");
        laik_log_Slice(ba->slc);
        laik_log_append(" mapNo %d, count %d ==> buf %d off %lld",
                        ba->fromMapNo, ba->count, ba->bufID, ba->offset);
        break;

    case LAIK_AT_MapPackToBuf:
        laik_log_append(": ");
        laik_log_Slice(ba->slc);
        laik_log_append(" mapNo %d, count %d ==> buf %p",
                        ba->fromMapNo, ba->count, (void*) ba->toBuf);
        break;

    case LAIK_AT_MapPackAndSend: {
        Laik_A_MapPackAndSend* aa = (Laik_A_MapPackAndSend*) a;
        laik_log_append(": ");
        laik_log_Slice(aa->slc);
        laik_log_append(" mapNo %d, count %llu ==> T%d",
                        aa->fromMapNo, (unsigned long long) aa->count, aa->to_rank);
        break;
    }

    case LAIK_AT_PackAndSend:
        laik_log_append(": ");
        laik_log_Slice(ba->slc);
        laik_log_append(" count %d ==> T%d",
                        ba->count, ba->rank);
        break;

    case LAIK_AT_UnpackFromBuf:
        laik_log_append(": buf %p ==> ", (void*) ba->fromBuf);
        laik_log_Slice(ba->slc);
        laik_log_append(", count %d", ba->count);
        break;

    case LAIK_AT_UnpackFromRBuf:
        laik_log_append(": buf %d, off %lld ==> ", ba->bufID, ba->offset);
        laik_log_Slice(ba->slc);
        laik_log_append(", count %d", ba->count);
        break;

    case LAIK_AT_MapUnpackFromRBuf:
        laik_log_append(": buf %d, off %lld ==> ", ba->bufID, ba->offset);
        laik_log_Slice(ba->slc);
        laik_log_append(" mapNo %d, count %d", ba->toMapNo, ba->count);
        break;

    case LAIK_AT_MapUnpackFromBuf:
        laik_log_append(": buf %p ==> ", (void*) ba->fromBuf);
        laik_log_Slice(ba->slc);
        laik_log_append(" mapNo %d, count %d", ba->toMapNo, ba->count);
        break;

    case LAIK_AT_RecvAndUnpack:
        laik_log_append(": T%d ==> ", ba->rank);
        laik_log_Slice(ba->slc);
        laik_log_append(", count %d", ba->count);
        break;

    case LAIK_AT_MapRecvAndUnpack: {
        Laik_A_MapRecvAndUnpack* aa = (Laik_A_MapRecvAndUnpack*) a;
        laik_log_append(": T%d ==> ", aa->from_rank);
        laik_log_Slice(aa->slc);
        laik_log_append(" mapNo %d, count %llu",
                        aa->toMapNo, (unsigned long long) aa->count);
        break;
    }

    default:
        laik_log(LAIK_LL_Panic,
                 "laik_log_Action: unknown action %d (%s)",
                 a->type, laik_at_str(a->type));
        assert(0);
    }
}

void laik_log_ActionSeq(Laik_ActionSeq *as, bool showDetails)
{
    laik_log_append("action seq for %d transition(s), backend cleanup: %s\n"
                    "  %d rounds, %d buffers (%.3f MB),"
                    " %d actions (%d B), %d ranges (%d B)\n",
                    as->contextCount, as->backend ? as->backend->name : "none",
                    as->roundCount,
                    as->bufferCount, 0.000001 * laik_aseq_bufsize(as),
                    as->actionCount, as->bytesUsed,
                    as->ceRanges, sizeof(Laik_CopyEntry) * as->ceRanges);

    Laik_TransitionContext* tc = 0;
    for(int i = 0; i < as->contextCount; i++) {
        tc = as->context[i];
        laik_log_append("  transition %d: ", 0);
        laik_log_Transition(tc->transition, false);
        laik_log_append(" on data '%s'\n", tc->data->name);
    }
    assert(as->contextCount == 1);
    if (!showDetails) return;

    for(int i = 0; i < as->bufferCount; i++) {
        laik_log_append("  buffer %d: len %d at %p\n",
                        i, as->bufSize[i], as->buf[i]);
    }

    Laik_Action* a = as->action;
    for(int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        laik_log_Action(a, as);
        laik_log_append("\n");
    }
    assert(as->bytesUsed == ((char*)a) - ((char*)as->action));
}


void laik_log_Checksum(char* buf, int count, Laik_Type* t)
{
    assert(t == laik_Double);
    double sum = 0.0;
    for(int i = 0; i < count; i++)
        sum += ((double*)buf)[i];
    laik_log_append("checksum %f", sum);
}


// logging helpers not just appending

// write action sequence at level 1 if <changed> is true, prepend with title
void laik_log_ActionSeqIfChanged(bool changed, Laik_ActionSeq* as, char* title)
{
    if (laik_log_begin(1)) {
        laik_log_append(title);
        if (changed) {
            laik_log_append(":\n");
            laik_log_ActionSeq(as, true);
        }
        else
            laik_log_append(": nothing changed\n");
        laik_log_flush(0);
    }
}

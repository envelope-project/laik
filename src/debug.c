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

void laik_log_Slice(int dims, Laik_Slice* slc)
{
    if (laik_slice_isEmpty(dims, slc)) {
        laik_log_append("(empty)");
        return;
    }

    laik_log_append("[");
    laik_log_Index(dims, &(slc->from));
    laik_log_append(";");
    laik_log_Index(dims, &(slc->to));
    laik_log_append("[");
}

void laik_log_Reduction(Laik_ReductionOperation op)
{
    switch(op) {
    case LAIK_RO_None: laik_log_append("none"); break;
    case LAIK_RO_Sum:  laik_log_append("sum"); break;
    default: assert(0);
    }
}


void laik_log_DataFlow(Laik_DataFlow flow)
{
    bool out = false;

    if (flow & LAIK_DF_CopyIn) {
        laik_log_append("copyin");
        out = true;
    }
    if (flow & LAIK_DF_CopyOut) {
        if (out) laik_log_append("|");
        laik_log_append("copyout");
        out = true;
    }
    if (flow & LAIK_DF_Init) {
        if (out) laik_log_append("|");
        laik_log_append("init");
        out = true;
    }
    if (flow & LAIK_DF_ReduceOut) {
        if (out) laik_log_append("|");
        laik_log_append("reduceout");
        out = true;
    }
    if (flow & LAIK_DF_Sum) {
        if (out) laik_log_append("|");
        laik_log_append("sum");
        out = true;
    }
    if (!out)
        laik_log_append("none");
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
    laik_log_append("transition (%s/",
                    t->fromPartitioning ? t->fromPartitioning->name : "none");
    laik_log_DataFlow(t->fromFlow);
    laik_log_append(" => %s/",
                    t->toPartitioning->name ? t->toPartitioning->name : "none");
    laik_log_DataFlow(t->toFlow);
    if (!showActions) {
        laik_log_append(")");
        return;
    }

    laik_log_append("): ");

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
            laik_log_Slice(t->dims, &(t->local[i].slc));
        }
    }

    if (t->initCount>0) {
        laik_log_append("\n   %2d init : ", t->initCount);
        for(int i=0; i<t->initCount; i++) {
            if (i>0) laik_log_append(", ");
            laik_log_Reduction(t->init[i].redOp);
            laik_log_Slice(t->dims, &(t->init[i].slc));
        }
    }

    if (t->sendCount>0) {
        laik_log_append("\n   %2d send : ", t->sendCount);
        for(int i=0; i<t->sendCount; i++) {
            if (i>0) laik_log_append(", ");
            laik_log_Slice(t->dims, &(t->send[i].slc));
            laik_log_append("==>T%d", t->send[i].toTask);
        }
    }

    if (t->recvCount>0) {
        laik_log_append("\n   %2d recv : ", t->recvCount);
        for(int i=0; i<t->recvCount; i++) {
            if (i>0) laik_log_append(", ");
            laik_log_append("T%d==>", t->recv[i].fromTask);
            laik_log_Slice(t->dims, &(t->recv[i].slc));
        }
    }

    if (t->redCount>0) {
        laik_log_append("\n   %2d reduc: ", t->redCount);
        for(int i=0; i<t->redCount; i++) {
            if (i>0) laik_log_append(", ");
            laik_log_Slice(t->dims, &(t->red[i].slc));
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
    laik_log_append(": (task:slice:tag/mapNo)\n    ");
    for(int i = 0; i < p->count; i++) {
        Laik_TaskSlice_Gen* ts = &(p->tslice[i]);
        if (i>0)
            laik_log_append(", ");
        laik_log_append("%d:", ts->task);
        laik_log_Slice(p->space->dims, &(ts->s));
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


void laik_log_Action(Laik_Action* a, Laik_TransitionContext* tc)
{
    Laik_BackendAction* ba = (Laik_BackendAction*) a;
    switch(ba->type) {
    case LAIK_AT_Nop:
        laik_log_append("    NOP");
        break;

    case LAIK_AT_BufReserve:
        laik_log_append("    reserve: buf id %d, size %d",
                        ba->bufID,
                        ba->count);
        break;

    case LAIK_AT_MapSend:
        laik_log_append("    send (R %d): from map %d, off %d, count %d, to T%d",
                        ba->round,
                        ba->mapNo,
                        ba->offset,
                        ba->count,
                        ba->peer_rank);
        break;

    case LAIK_AT_BufSend:
        laik_log_append("    send (R %d): from %p, count %d, to T%d",
                        ba->round,
                        ba->fromBuf,
                        ba->count,
                        ba->peer_rank);
        break;

    case LAIK_AT_RBufSend:
        laik_log_append("    send (R %d): from buf %d, off %lld, count %d, to T%d",
                        ba->round,
                        ba->bufID, (long long int) ba->offset,
                        ba->count,
                        ba->peer_rank);
        break;


    case LAIK_AT_MapRecv:
        laik_log_append("    recv (R %d): from T%d, to map %d, off %d, count %d",
                        ba->round,
                        ba->peer_rank,
                        ba->mapNo,
                        ba->offset,
                        ba->count);
        break;

    case LAIK_AT_BufRecv:
        laik_log_append("    recv (R %d): from T%d, to %p, count %d",
                        ba->round,
                        ba->peer_rank,
                        ba->toBuf,
                        ba->count);
        break;

    case LAIK_AT_RBufRecv:
        laik_log_append("    recv (R %d): from T%d, to buf %d, off %lld, count %d",
                        ba->round,
                        ba->peer_rank,
                        ba->bufID, (long long int) ba->offset,
                        ba->count);
        break;

    case LAIK_AT_CopyFromBuf:
        laik_log_append("    copy from buffer (R %d): buf %p, ranges %d",
                        ba->round,
                        ba->fromBuf,
                        ba->count);
        for(int i = 0; i < ba->count; i++)
            laik_log_append("\n        off %d, bytes %d => to %p",
                            ba->ce[i].offset,
                            ba->ce[i].bytes,
                            ba->ce[i].ptr);
        break;

    case LAIK_AT_CopyToBuf:
        laik_log_append("    copy to buffer (R %d): buf %p, ranges %d",
                        ba->round,
                        ba->toBuf,
                        ba->count);
        for(int i = 0; i < ba->count; i++)
            laik_log_append("\n        %p => off %d, bytes %d",
                            ba->ce[i].ptr,
                            ba->ce[i].offset,
                            ba->ce[i].bytes);
        break;

    case LAIK_AT_CopyFromRBuf:
        laik_log_append("    copy from buffer (R %d): buf %d, off %lld, ranges %d",
                        ba->round,
                        ba->bufID, (long long int) ba->offset,
                        ba->count);
        for(int i = 0; i < ba->count; i++)
            laik_log_append("\n        off %d, bytes %d => to %p",
                            ba->ce[i].offset,
                            ba->ce[i].bytes,
                            ba->ce[i].ptr);
        break;

    case LAIK_AT_CopyToRBuf:
        laik_log_append("    copy to buffer (R %d): buf %d, off %lld, ranges %d",
                        ba->round,
                        ba->bufID, (long long int) ba->offset,
                        ba->count);
        for(int i = 0; i < ba->count; i++)
            laik_log_append("\n        %p => off %d, bytes %d",
                            ba->ce[i].ptr,
                            ba->ce[i].offset,
                            ba->ce[i].bytes);
        break;

    case LAIK_AT_BufCopy:
        laik_log_append("    copy (R %d): from %p, to %p, count %d",
                        ba->round,
                        ba->fromBuf,
                        ba->toBuf,
                        ba->count);
        break;

    case LAIK_AT_RBufCopy:
        laik_log_append("    copy (R %d): from %p (buf %d off %lld), to %p, count %d",
                        ba->round,
                        ba->fromBuf,
                        ba->bufID, (long long int) ba->offset,
                        ba->toBuf,
                        ba->count);
        break;

    case LAIK_AT_Copy:
        laik_log_append("    copy: count %d", ba->count);
        break;

    case LAIK_AT_Reduce:
        laik_log_append("    reduce: count %d, from %p, to %p, root %d",
                        ba->count, ba->fromBuf, ba->toBuf, ba->peer_rank);
        break;

    case LAIK_AT_GroupReduce:
        laik_log_append("    groupReduce: count %d, from %p, to %p, input ",
                        ba->count, ba->fromBuf, ba->toBuf);
        laik_log_TransitionGroup(tc->transition, ba->inputGroup);
        laik_log_append(", output ");
        laik_log_TransitionGroup(tc->transition, ba->outputGroup);
        break;

    case LAIK_AT_RBufGroupReduce:
        laik_log_append("    groupReduce: count %d, from/to buf %d, off %lld, input ",
                        ba->count, ba->bufID, (long long int) ba->offset);
        laik_log_TransitionGroup(tc->transition, ba->inputGroup);
        laik_log_append(", output ");
        laik_log_TransitionGroup(tc->transition, ba->outputGroup);
        break;

    case LAIK_AT_RBufReduce:
        laik_log_append("    reduce (R %d): type %s, redOp ",
                        ba->round, ba->dtype->name);
        laik_log_Reduction(ba->redOp);
        laik_log_append(", from %p (%d off %lld), to %p, count %d",
                        (void*) ba->fromBuf,
                        ba->bufID, (long long int) ba->offset,
                        ba->toBuf,
                        ba->count);
        break;

    case LAIK_AT_BufInit:
        laik_log_append("    init (R %d): type %s, redOp ",
                        ba->round, ba->dtype->name);
        laik_log_Reduction(ba->redOp);
        laik_log_append(", to %p, count %d",
                        ba->toBuf,
                        ba->count);
        break;

    case LAIK_AT_MapPackAndSend:
        laik_log_append("    packAndSend (R %d): ", ba->round);
        laik_log_Slice(ba->dims, ba->slc);
        laik_log_append(" mapNo %d => T%d, count %d",
                        ba->mapNo, ba->peer_rank, ba->count);
        break;

    case LAIK_AT_PackAndSend:
        laik_log_append("    packAndSend (R %d): ", ba->round);
        laik_log_Slice(ba->dims, ba->slc);
        laik_log_append(" => T%d, count %d",
                        ba->peer_rank, ba->count);
        break;

    case LAIK_AT_RecvAndUnpack:
        laik_log_append("    recvAndUnpack (R %d): T%d => ",
                        ba->round, ba->peer_rank);
        laik_log_Slice(ba->dims, ba->slc);
        laik_log_append(", count %d", ba->count);
        break;

    case LAIK_AT_MapRecvAndUnpack:
        laik_log_append("    recvAndUnpack (R %d): T%d => ",
                        ba->round, ba->peer_rank);
        laik_log_Slice(ba->dims, ba->slc);
        laik_log_append(" mapNo %d, count %d", ba->mapNo, ba->count);
        break;

    default:
        laik_log(LAIK_LL_Panic,
                 "laik_log_Action: unknown action %d", ba->type);
        assert(0);
    }
}

void laik_log_ActionSeq(Laik_ActionSeq *as)
{
    Laik_TransitionContext* tc = as->context[0];
    laik_log_append("actions for ");
    laik_log_Transition(tc->transition, false);
    laik_log_append(" on '%s', bufsize %d (%d actions)\n",
                    tc->data->name, as->bufSize, as->actionCount);

    for(int i = 0; i < as->actionCount; i++) {
        laik_log_Action((Laik_Action*) &(as->action[i]), tc);
        laik_log_append("\n");
    }
}

void laik_log_Checksum(char* buf, int count, Laik_Type* t)
{
    assert(t == laik_Double);
    double sum = 0.0;
    for(int i = 0; i < count; i++)
        sum += ((double*)buf)[i];
    laik_log_append("checksum %f", sum);
}

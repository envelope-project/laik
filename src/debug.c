/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
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

void laik_log_Index(int dims, Laik_Index* idx)
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

    assert(group < t->groupCount);
    TaskGroup* tg = &(t->group[group]);

    laik_log_append("(");
    for(int i = 0; i < tg->count; i++) {
        if (i > 0) laik_log_append(",");
        laik_log_append("T%d", tg->task[i]);
    }
    laik_log_append(")");
}

void laik_log_Transition(Laik_Transition* t)
{
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
    laik_log_append(": (task:slice:tag/mapNo/start)\n    ");
    for(int i = 0; i < p->count; i++) {
        Laik_TaskSlice_Gen* ts = &(p->tslice[i]);
        if (i>0)
            laik_log_append(", ");
        laik_log_append("%d:", ts->task);
        laik_log_Slice(p->space->dims, &(ts->s));
        laik_log_append(":%d/%d/%d", ts->tag, ts->mapNo, ts->compactStart);
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


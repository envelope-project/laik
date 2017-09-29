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


int laik_getIntListStr(char* s, int len, int* list)
{
    int o;
    o = sprintf(s, "[");
    for(int i = 0; i < len; i++) {
        o += sprintf(s+o, "%s%d",
                     (i>0) ? ", ":"", list[i]);
    }
    o += sprintf(s+o, "]");
    return o;
}


int laik_getSpaceStr(char* s, Laik_Space* spc)
{
    switch(spc->dims) {
    case 1:
        return sprintf(s, "[%llu;%llu[",
                       (unsigned long long) spc->s.from.i[0],
                       (unsigned long long) spc->s.to.i[0] );
    case 2:
        return sprintf(s, "[%llu;%llu[ x [%llu;%llu[",
                       (unsigned long long) spc->s.from.i[0],
                       (unsigned long long) spc->s.to.i[0],
                       (unsigned long long) spc->s.from.i[1],
                       (unsigned long long) spc->s.to.i[1] );
    case 3:
        return sprintf(s, "[%llu;%llu[ x [%llu;%llu[ x [%llu;%llu[",
                       (unsigned long long) spc->s.from.i[0],
                       (unsigned long long) spc->s.to.i[0],
                       (unsigned long long) spc->s.from.i[1],
                       (unsigned long long) spc->s.to.i[1],
                       (unsigned long long) spc->s.from.i[2],
                       (unsigned long long) spc->s.to.i[2] );
    default: assert(0);
    }
    return 0;
}

int laik_getIndexStr(char* s, int dims, Laik_Index* idx)
{
    uint64_t i1 = idx->i[0];
    uint64_t i2 = idx->i[1];
    uint64_t i3 = idx->i[2];

    switch(dims) {
    case 1:
        return sprintf(s, "%llu", (unsigned long long) i1);
    case 2:
        return sprintf(s, "%llu/%llu",
                       (unsigned long long) i1,
                       (unsigned long long) i2);
    case 3:
        return sprintf(s, "%llu/%llu/%llu",
                       (unsigned long long) i1,
                       (unsigned long long) i2,
                       (unsigned long long) i3);
    default: assert(0);
    }
    return 0;
}

int laik_getSliceStr(char* s, int dims, Laik_Slice* slc)
{
    if (laik_slice_isEmpty(dims, slc))
        return sprintf(s, "(empty)");

    int off;
    off  = sprintf(s, "[");
    off += laik_getIndexStr(s+off, dims, &(slc->from));
    off += sprintf(s+off, ";");
    off += laik_getIndexStr(s+off, dims, &(slc->to));
    off += sprintf(s+off, "[");
    return off;
}

int laik_getReductionStr(char* s, Laik_ReductionOperation op)
{
    switch(op) {
    case LAIK_RO_None: return sprintf(s, "none");
    case LAIK_RO_Sum:  return sprintf(s, "sum");
    default: assert(0);
    }
    return 0;
}


int laik_getDataFlowStr(char* s, Laik_DataFlow flow)
{
    int o = 0;

    if (flow & LAIK_DF_CopyIn)    o += sprintf(s+o, "copyin|");
    if (flow & LAIK_DF_CopyOut)   o += sprintf(s+o, "copyout|");
    if (flow & LAIK_DF_Init)      o += sprintf(s+o, "init|");
    if (flow & LAIK_DF_ReduceOut) o += sprintf(s+o, "reduceout|");
    if (flow & LAIK_DF_Sum)       o += sprintf(s+o, "sum|");
    if (o > 0) {
        o--;
        s[o] = 0;
    }
    else
        o += sprintf(s+o, "none");

    return o;
}


int laik_getTransitionStr(char* s, Laik_Transition* t)
{
    if (t == 0)
        return 0;

    int off = 0;

    if (t->localCount>0) {
        off += sprintf(s+off, "   %2d local: ", t->localCount);
        for(int i=0; i<t->localCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += laik_getSliceStr(s+off, t->dims, &(t->local[i].slc));
        }
        off += sprintf(s+off, "\n");
    }

    if (t->initCount>0) {
        off += sprintf(s+off, "   %2d init : ", t->initCount);
        for(int i=0; i<t->initCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += laik_getReductionStr(s+off, t->init[i].redOp);
            off += laik_getSliceStr(s+off, t->dims, &(t->init[i].slc));
        }
        off += sprintf(s+off, "\n");
    }

    if (t->sendCount>0) {
        off += sprintf(s+off, "   %2d send : ", t->sendCount);
        for(int i=0; i<t->sendCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += laik_getSliceStr(s+off, t->dims, &(t->send[i].slc));
            off += sprintf(s+off, "==>T%d", t->send[i].toTask);
        }
        off += sprintf(s+off, "\n");
    }

    if (t->recvCount>0) {
        off += sprintf(s+off, "   %2d recv : ", t->recvCount);
        for(int i=0; i<t->recvCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += sprintf(s+off, "T%d==>", t->recv[i].fromTask);
            off += laik_getSliceStr(s+off, t->dims, &(t->recv[i].slc));
        }
        off += sprintf(s+off, "\n");
    }

    if (t->redCount>0) {
        off += sprintf(s+off, "   %2d reduc: ", t->redCount);
        for(int i=0; i<t->redCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += laik_getReductionStr(s+off, t->red[i].redOp);
            off += laik_getSliceStr(s+off, t->dims, &(t->red[i].slc));
            off += sprintf(s+off, "=> %s",
                           (t->red[i].rootTask == -1) ? "all":"master");
        }
        off += sprintf(s+off, "\n");
    }

    if (off == 0) s[0] = 0;
    return off;
}

int laik_getBorderArrayStr(char* s, Laik_BorderArray* ba)
{
    int o;

    if (!ba)
        return sprintf(s, "(no borders)");

    o = sprintf(s, "%d slices in %d tasks on ",
                ba->count, ba->group->size);
    o += laik_getSpaceStr(s+o, ba->space);
    o += sprintf(s+o,": (task:slice:tag/mapNo)\n    ");
    for(int i = 0; i < ba->count; i++) {
        if (i>0)
            o += sprintf(s+o, ", ");
        o += sprintf(s+o, "%d:", ba->tslice[i].task);
        o += laik_getSliceStr(s+o, ba->space->dims, &(ba->tslice[i].s));
        o += sprintf(s+o, ":%d/%d", ba->tslice[i].tag, ba->tslice[i].mapNo);
    }

    return o;
}


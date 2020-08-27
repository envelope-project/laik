/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017-2020 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
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

#include "laik-internal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// a layout is a serialisation order of a LAIK container,
// e.g. to define how indexes are layed out in memory
//
// - generic interface
// - implementation of lexicographical layout

void laik_init_layout(Laik_Layout* l, int dims,
                      laik_layout_pack_t pack,
                      laik_layout_unpack_t unpack,
                      laik_layout_describe_t describe,
                      laik_layout_offset_t offset,
                      laik_layout_first_t first,
                      laik_layout_next_t next)
{
    l->dims = dims;
    l->pack = pack;
    l->unpack = unpack;
    l->describe = describe;
    l->offset = offset;
    l->first = first;
    l->next = next;
}


// interface implementation of lexicographical layout

static
int64_t laik_offset_lex(Laik_Layout* l, Laik_Index* idx)
{
    Laik_Layout_Lex* ll = laik_is_layout_lex(l);
    assert(ll);
    int dims = l->dims;

    int64_t off = idx->i[0] * ll->stride[0];
    if (dims > 1) {
        off += idx->i[1] * ll->stride[1];
        if (dims > 2) {
            off += idx->i[2] * ll->stride[2];
        }
    }
    return off;
}

static
int64_t laik_first_lex(Laik_Layout* l,
                       Laik_Slice* slc, Laik_Index* idx)
{
    *idx = slc->from;
    return laik_offset_lex(l, idx);
}

// return number of possible increments in x
static
int correct_idx(Laik_Slice* slc, Laik_Index* idx)
{
    assert(idx->i[0] >= slc->from.i[0]);
    if (idx->i[0] >= slc->to.i[0]) {
        idx->i[0] = slc->from.i[0];
        idx->i[1]++;
    }

    assert(idx->i[1] >= slc->from.i[1]);
    if (idx->i[1] >= slc->to.i[1]) {
        idx->i[0] = slc->from.i[0];
        idx->i[1] = slc->from.i[1];
        idx->i[2]++;
    }

    assert(idx->i[2] >= slc->from.i[2]);
    if (idx->i[2] >= slc->to.i[2]) return 0;

    return slc->to.i[0] - idx->i[0];
}

static
int64_t laik_next_lex(Laik_Layout* l,
                      Laik_Slice* slc, Laik_Index* idx, int max)
{
    (void) l; // not used, but part of interface

    int steps = correct_idx(slc, idx);
    if (steps == 0) return 0;

    if (steps > max) steps = max;
    idx->i[0] += steps;

    // ensure idx is valid if traversal not finished:
    //  user may want to call laik_offset on it
    correct_idx(slc, idx);

    return steps;
}

// pack/unpack routines for lexicographical layout
static
unsigned int laik_pack_lex(const Laik_Mapping* m, const Laik_Slice* s,
                           Laik_Index* idx, char* buf, unsigned int size)
{
    unsigned int elemsize = m->data->elemsize;
    Laik_Layout_Lex* layout = laik_is_layout_lex(m->layout);
    int dims = m->layout->dims;

    if (laik_index_isEqual(dims, idx, &(s->to))) {
        // nothing left to pack
        return 0;
    }

    // TODO: only default layout with order 1/2/3
    assert(layout->stride[0] == 1);
    if (dims > 1) {
        assert(layout->stride[0] <= layout->stride[1]);
        if (dims > 2)
            assert(layout->stride[1] <= layout->stride[2]);
    }

    // slice to pack must within local valid slice of mapping
    assert(laik_slice_within_slice(s, &(m->requiredSlice)));

    // calculate address of starting index
    Laik_Index localIdx;
    laik_sub_index(&localIdx, idx, &(m->requiredSlice.from));
    uint64_t idxOff = laik_offset(&localIdx, m->layout);
    char* idxPtr = m->base + idxOff * elemsize;

    int64_t i0, i1, i2, from0, from1, to0, to1, to2, count;
    from0 = s->from.i[0];
    from1 = s->from.i[1];
    to0 = s->to.i[0];
    to1 = s->to.i[1];
    to2 = s->to.i[2];
    i0 = idx->i[0];
    i1 = idx->i[1];
    i2 = idx->i[2];
    if (dims < 3) {
        to2 = 1; i2 = 0;
        if (dims < 2) {
            from1 = 0; to1 = 1; i1 = 0;
        }
    }
    count = 0;

    // elements to skip after to0 reached
    int64_t skip0 = layout->stride[1] - (to0 - from0);
    // elements to skip after to1 reached
    int64_t skip1 = layout->stride[2] - layout->stride[1] * (to1 - from1);

    if (laik_log_begin(1)) {
        Laik_Index slcsize, localFrom;
        laik_sub_index(&localFrom, &(s->from), &(m->requiredSlice.from));
        laik_sub_index(&slcsize, &(s->to), &(s->from));

        laik_log_append("        packing '%s', size (", m->data->name);
        laik_log_Index(dims, &slcsize);
        laik_log_append(") x %d from global (", elemsize);
        laik_log_Index(dims, &(s->from));
        laik_log_append(") / local (");
        laik_log_Index(dims, &localFrom);
        laik_log_append(")/%d, start (", m->mapNo);
        laik_log_Index(dims, idx);
        laik_log_flush(") off %lu, buf size %d", idxOff, size);
    }

    bool stop = false;
    for(; i2 < to2; i2++) {
        for(; i1 < to1; i1++) {
            for(; i0 < to0; i0++) {
                if (size < elemsize) {
                    stop = true;
                    break;
                }

#if DEBUG_PACK
                laik_log(1, "packing (%lu/%lu/%lu) off %lu: %.3f, left %d",
                         i0, i1, i2,
                         (idxPtr - m->base)/elemsize, *(double*)idxPtr,
                         size - elemsize);
#endif

                // copy element into buffer
                memcpy(buf, idxPtr, elemsize);

                idxPtr += elemsize; // stride[0] is 1
                size -= elemsize;
                buf += elemsize;
                count++;

            }
            if (stop) break;
            idxPtr += skip0 * elemsize;
            i0 = from0;
        }
        if (stop) break;
        idxPtr += skip1 * elemsize;
        i1 = from1;
    }
    if (!stop) {
        // we reached end, set i0/i1 to last positions
        i0 = to0;
        i1 = to1;
    }

    if (laik_log_begin(1)) {
        Laik_Index idx2;
        laik_index_init(&idx2, i0, i1, i2);

        laik_log_append("        packed '%s': end (", m->data->name);
        laik_log_Index(dims, &idx2);
        laik_log_flush("), %lu elems = %lu bytes, %d left",
                       count, count * elemsize, size);
    }

    // save position we reached
    idx->i[0] = i0;
    idx->i[1] = i1;
    idx->i[2] = i2;
    return count;
}

static
unsigned int laik_unpack_lex(const Laik_Mapping* m, const Laik_Slice* s,
                             Laik_Index* idx, char* buf, unsigned int size)
{
    unsigned int elemsize = m->data->elemsize;
    Laik_Layout_Lex* layout = laik_is_layout_lex(m->layout);
    int dims = m->layout->dims;

    // there should be something to unpack
    assert(size > 0);
    assert(!laik_index_isEqual(dims, idx, &(s->to)));

    // TODO: only default layout with order 1/2/3
    assert(layout->stride[0] == 1);
    if (dims > 1) {
        assert(layout->stride[0] <= layout->stride[1]);
        if (dims > 2)
            assert(layout->stride[1] <= layout->stride[2]);
    }

    // slice to unpack into must be within local valid slice of mapping
    assert(laik_slice_within_slice(s, &(m->requiredSlice)));

    // calculate address of starting index
    Laik_Index localIdx;
    laik_sub_index(&localIdx, idx, &(m->requiredSlice.from));
    uint64_t idxOff = laik_offset(&localIdx, m->layout);
    char* idxPtr = m->base + idxOff * elemsize;

    int64_t i0, i1, i2, from0, from1, to0, to1, to2, count;
    from0 = s->from.i[0];
    from1 = s->from.i[1];
    to0 = s->to.i[0];
    to1 = s->to.i[1];
    to2 = s->to.i[2];
    i0 = idx->i[0];
    i1 = idx->i[1];
    i2 = idx->i[2];
    if (dims < 3) {
        to2 = 1; i2 = 0;
        if (dims < 2) {
            from1 = 0; to1 = 1; i1 = 0;
        }
    }
    count = 0;

    // elements to skip after to0 reached
    uint64_t skip0 = layout->stride[1] - (to0 - from0);
    // elements to skip after to1 reached
    uint64_t skip1 = layout->stride[2] - layout->stride[1] * (to1 - from1);

    if (laik_log_begin(1)) {
        Laik_Index slcsize, localFrom;
        laik_sub_index(&localFrom, &(s->from), &(m->requiredSlice.from));
        laik_sub_index(&slcsize, &(s->to), &(s->from));

        laik_log_append("        unpacking '%s', size (", m->data->name);
        laik_log_Index(dims, &slcsize);
        laik_log_append(") x %d from global (", elemsize);
        laik_log_Index(dims, &(s->from));
        laik_log_append(") / local (");
        laik_log_Index(dims, &localFrom);
        laik_log_append(")/%d, start (", m->mapNo);
        laik_log_Index(dims, idx);
        laik_log_flush(") off %lu, buf size %d", idxOff, size);

    }

    bool stop = false;
    for(; i2 < to2; i2++) {
        for(; i1 < to1; i1++) {
            for(; i0 < to0; i0++) {
                if (size < elemsize) {
                    stop = true;
                    break;
                }

#ifdef DEBUG_UNPACK
                laik_log(1, "unpacking (%lu/%lu/%lu) off %lu: %.3f, left %d",
                         i0, i1, i2,
                         (idxPtr - m->base)/elemsize, *(double*)buf,
                         size - elemsize);
#endif
                // copy element from buffer into local data
                memcpy(idxPtr, buf, elemsize);

                idxPtr += elemsize; // stride[0] is 1
                size -= elemsize;
                buf += elemsize;
                count++;

            }
            if (stop) break;
            idxPtr += skip0 * elemsize;
            i0 = from0;
        }
        if (stop) break;
        idxPtr += skip1 * elemsize;
        i1 = from1;
    }
    if (!stop) {
        // we reached end, set i0/i1 to last positions
        i0 = to0;
        i1 = to1;
    }

    if (laik_log_begin(1)) {
        Laik_Index idx2;
        laik_index_init(&idx2, i0, i1, i2);

        laik_log_append("        unpacked '%s': end (", m->data->name);
        laik_log_Index(dims, &idx2);
        laik_log_flush("), %lu elems = %lu bytes, %d left",
                       count, count * elemsize, size);
    }

    // save position we reached
    idx->i[0] = i0;
    idx->i[1] = i1;
    idx->i[2] = i2;
    return count;
}

static
char* laik_layout_describe_lex(Laik_Layout* l)
{
    static char s[100];

    assert(l->describe == laik_layout_describe_lex);
    Laik_Layout_Lex* layout = (Laik_Layout_Lex*) l;

    sprintf(s, "lex %dd, strides (%llu/%llu/%llu)",
             l->dims,
             (unsigned long long) layout->stride[0],
             (unsigned long long) layout->stride[1],
             (unsigned long long) layout->stride[2]);

    return s;
}

// allocate new layout object describing lexicographical layout
// helper for laik_new_layout_lex1d/2d/3d
static
Laik_Layout_Lex* laik_new_layout_lex(int dims)
{
    Laik_Layout_Lex* l = malloc(sizeof(Laik_Layout_Lex));
    if (!l) {
        laik_panic("Out of memory allocating Laik_Layout_Lex object");
        exit(1); // not actually needed, laik_panic never returns
    }
    laik_init_layout(&(l->h), dims,
                     laik_pack_lex, laik_unpack_lex,
                     laik_layout_describe_lex, laik_offset_lex,
                     laik_first_lex, laik_next_lex);

    return l;
}

// create layout object for dense 1d lexicographical layout
Laik_Layout* laik_new_layout_lex1d()
{
    Laik_Layout_Lex* l = laik_new_layout_lex(1);
    l->stride[0] = 1;
    l->stride[1] = 0;
    l->stride[2] = 0;

    return (Laik_Layout*) l;
}

// create layout object for 2d lexicographical layout
// with stride 1 in dimension X and <stride> in dimension Y
Laik_Layout* laik_new_layout_lex2d(uint64_t stride)
{
    Laik_Layout_Lex* l = laik_new_layout_lex(2);
    l->stride[0] = 1;
    l->stride[1] = stride;
    l->stride[2] = 0;

    return (Laik_Layout*) l;
}

// create layout object for 3d lexicographical layout
// with strides 1 in X, <stride1> in Y and <stride2> in Z
Laik_Layout* laik_new_layout_lex3d(uint64_t stride1, uint64_t stride2)
{
    Laik_Layout_Lex* l = laik_new_layout_lex(3);
    l->stride[0] = 1;
    l->stride[1] = stride1;
    l->stride[2] = stride1 * stride2;

    return (Laik_Layout*) l;
}

// return lex layout if given layout is a lexicographical layout
Laik_Layout_Lex* laik_is_layout_lex(Laik_Layout* l)
{
    if (l->pack == laik_pack_lex)
        return (Laik_Layout_Lex*) l;

    return 0; // not a lexicographical layout
}

// return stride for dimension <d> in lex layout
uint64_t laik_layout_lex_stride(Laik_Layout* l, int d)
{
    Laik_Layout_Lex* ll = laik_is_layout_lex(l);
    assert((d >= 0) && (d < l->dims));

    return ll->stride[d];
}

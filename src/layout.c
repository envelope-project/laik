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

// this file:
// - generic interface implementations
// - implementation of lexicographical layout


// generic variants of layout interface functions
// just using offset function

// helper for laik_layout_copy/pack_gen: lexicographical traversal
static
bool next_lex(Laik_Slice* slc, Laik_Index* idx)
{
    idx->i[0]++;
    if (idx->i[0] < slc->to.i[0]) return true;
    if (slc->space->dims == 1) return false;

    idx->i[1]++;
    idx->i[0] = slc->from.i[0];
    if (idx->i[1] < slc->to.i[1]) return true;
    if (slc->space->dims == 2) return false;

    idx->i[2]++;
    idx->i[1] = slc->from.i[1];
    if (idx->i[2] < slc->to.i[2]) return true;
    return false;
}

// generic copy just using offset function from layout interface
void laik_layout_copy_gen(Laik_Slice* slc,
                          Laik_Mapping* from, Laik_Mapping* to)
{
    Laik_Layout* fromLayout = from->layout;
    Laik_Layout* toLayout = to->layout;
    unsigned int elemsize = from->data->elemsize;
    assert(elemsize == to->data->elemsize);

    if (laik_log_begin(1)) {
        laik_log_append("generic copy of slice ");
        laik_log_Slice(slc);
        laik_log_append(" (count %llu, elemsize %d) from mapping %p",
            laik_slice_size(slc), elemsize, from->start);
        laik_log_append(" (data '%s'/%d, %s) ",
            from->data->name, from->mapNo,
            from->layout->describe(from->layout));
        laik_log_flush("to mapping %p (data '%s'/%d, layout %s): ",
            to->start, to->data->name, to->mapNo,
            to->layout->describe(to->layout));
    }

    Laik_Index idx = slc->from;
    uint64_t count = 0;
    do {
        int64_t fromOffset = fromLayout->offset(fromLayout, &idx);
        int64_t toOffset = toLayout->offset(toLayout, &idx);
        void* fromPtr = from->start + fromOffset * elemsize;
        void* toPtr = to->start + toOffset * elemsize;
#if 0
        if (laik_log_begin(1)) {
            laik_log_append(" copy idx ");
            laik_log_Index(slc->space->dims, &idx);
            laik_log_flush(" from off %lu (ptr %p) to %lu (%p)",
                           fromOffset, fromPtr, toOffset, toPtr);
        }
#endif
        memcpy(toPtr, fromPtr, elemsize);
        count++;
    } while(next_lex(slc, &idx));
    assert(count == laik_slice_size(slc));
}

// generic pack just using offset function from layout interface.
// this packs data according to lexicographical traversal
// return number of elements packed into provided buffer
unsigned int laik_layout_pack_gen(Laik_Mapping* m, Laik_Slice* slc,
                                  Laik_Index* idx, char* buf, unsigned int size)
{
    unsigned int elemsize = m->data->elemsize;
    Laik_Layout* layout = m->layout;
    int dims = m->layout->dims;

    if (laik_index_isEqual(dims, idx, &(slc->to))) {
        // nothing left to pack
        return 0;
    }

    // slice to pack must within local valid slice of mapping
    assert(laik_slice_within_slice(slc, &(m->requiredSlice)));

    if (laik_log_begin(1)) {
        laik_log_append("        generic packing of slice ");
        laik_log_Slice(slc);
        laik_log_append(" (count %llu, elemsize %d) from mapping %p",
            laik_slice_size(slc), elemsize, m->start);
        laik_log_append(" (data '%s'/%d, %s) at idx ",
            m->data->name, m->mapNo, layout->describe(layout));
        laik_log_Index(dims, idx);
        laik_log_flush(" into buf (size %d)", size);
    }

    unsigned int count = 0;
    while(size >= elemsize) {
        int64_t off = layout->offset(layout, idx);
        void* idxPtr = m->start + off * elemsize;
#if 0
        if (laik_log_begin(1)) {
            laik_log_append(" idx ");
            laik_log_Index(dims, &idx);
            laik_log_flush(": off %lu (ptr %p), left %d", off, ptr, size);
        }
#endif
        // copy element into buffer
        memcpy(buf, idxPtr, elemsize);
        size -= elemsize;
        buf += elemsize;
        count++;

        if (!next_lex(slc, idx)) {
            *idx = slc->to;
            break;
        }
    }

    if (laik_log_begin(1)) {
        laik_log_append("        packed '%s': end (", m->data->name);
        laik_log_Index(dims, idx);
        laik_log_flush("), %lu elems = %lu bytes, %d left",
                       count, count * elemsize, size);
    }

    return count;
}

// generic unpack just using offset function from layout interface
// this expects provided data to be packed according to lexicographical traversal
// return number of elements unpacked from provided buffer
unsigned int laik_layout_unpack_gen(Laik_Mapping* m, Laik_Slice* slc,
                                    Laik_Index* idx, char* buf, unsigned int size)
{
    unsigned int elemsize = m->data->elemsize;
    Laik_Layout* layout = m->layout;
    int dims = m->layout->dims;

    // there should be something to unpack
    assert(size > 0);
    assert(!laik_index_isEqual(dims, idx, &(slc->to)));

    // slice to unpack into must be within local valid slice of mapping
    assert(laik_slice_within_slice(slc, &(m->requiredSlice)));

    if (laik_log_begin(1)) {
        laik_log_append("        generic unpacking of slice ");
        laik_log_Slice(slc);
        laik_log_append(" (count %llu, elemsize %d) into mapping %p",
            laik_slice_size(slc), elemsize, m->start);
        laik_log_append(" (data '%s'/%d, %s) at idx ",
            m->data->name, m->mapNo, layout->describe(layout));
        laik_log_Index(dims, idx);
        laik_log_flush(" from buf (size %d)", size);
    }

    unsigned int count = 0;
    while(size >= elemsize) {
        int64_t off = layout->offset(layout, idx);
        void* idxPtr = m->start + off * elemsize;
#if 0
        if (laik_log_begin(1)) {
            laik_log_append(" idx ");
            laik_log_Index(dims, &idx);
            laik_log_flush(": off %lu (ptr %p), left %d", off, ptr, size);
        }
#endif
        // copy element from buffer into mapping
        memcpy(idxPtr, buf, elemsize);
        size -= elemsize;
        buf += elemsize;
        count++;

        if (!next_lex(slc, idx)) {
            *idx = slc->to;
            break;
        }
    }

    if (laik_log_begin(1)) {
        laik_log_append("        unpacked '%s': end (", m->data->name);
        laik_log_Index(dims, idx);
        laik_log_flush("), %lu elems = %lu bytes, %d left",
                       count, count * elemsize, size);
    }

    return count;
}



// initialize generic members of a layout
void laik_init_layout(Laik_Layout* l, int dims, uint64_t count,
                      laik_layout_pack_t pack,
                      laik_layout_unpack_t unpack,
                      laik_layout_describe_t describe,
                      laik_layout_offset_t offset,
                      laik_layout_copy_t copy,
                      laik_layout_first_t first,
                      laik_layout_next_t next)
{
    l->dims = dims;
    l->count = count;

    // the offset function must be provided
    assert(offset != 0);

    // for testing, LAIK_LAYOUT_GENERIC enforces use of generic variants
    if (getenv("LAIK_LAYOUT_GENERIC")) {
        copy = 0;
        pack = 0;
        unpack = 0;
    }

    // if no specific version provided for pack/unpack/copy, use generic
    if (!pack)   pack   = laik_layout_pack_gen;
    if (!unpack) unpack = laik_layout_unpack_gen;
    if (!copy)   copy   = laik_layout_copy_gen;

    l->pack = pack;
    l->unpack = unpack;
    l->describe = describe;
    l->offset = offset;
    l->copy = copy;
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

    int64_t off = idx->i[0] - ll->slc.from.i[0];
    if (dims > 1) {
        off += (idx->i[1] - ll->slc.from.i[1]) * ll->stride[1];
        if (dims > 2) {
            off += (idx->i[2] - ll->slc.from.i[2]) * ll->stride[2];
        }
    }
    assert((off >= 0) && (off < (int64_t) l->count));
    return off;
}

static
void laik_layout_copy_lex(Laik_Slice* slc,
                          Laik_Mapping* from, Laik_Mapping* to)
{
    Laik_Layout_Lex* fromLayout = laik_is_layout_lex(from->layout);
    Laik_Layout_Lex* toLayout = laik_is_layout_lex(to->layout);
    assert(fromLayout != 0);
    assert(toLayout != 0);

    unsigned int elemsize = from->data->elemsize;
    assert(elemsize == to->data->elemsize);
    int dims = from->layout->dims;
    assert(dims == to->layout->dims);

    Laik_Index count;
    laik_sub_index(&count, &(slc->to), &(slc->from));
    if (dims < 3) {
        count.i[2] = 1;
        if (dims < 2)
            count.i[1] = 1;
    }
    uint64_t ccount = count.i[0] * count.i[1] * count.i[2];
    assert(ccount > 0);

    uint64_t fromOff  = laik_offset_lex(from->layout, &(slc->from));
    uint64_t toOff    = laik_offset_lex(to->layout, &(slc->from));
    char*    fromPtr  = from->start + fromOff * elemsize;
    char*    toPtr    = to->start   + toOff * elemsize;

    if (laik_log_begin(1)) {
        laik_log_append("lex copy of slice ");
        laik_log_Slice(slc);
        laik_log_append(" (count %llu, elemsize %d) from mapping %p",
            ccount, elemsize, from->start);
        laik_log_append(" (data '%s'/%d, %s) ",
            from->data->name, from->mapNo,
            from->layout->describe(from->layout));
        laik_log_append("to mapping %p (data '%s'/%d, layout %s): ",
            to->start, to->data->name, to->mapNo,
            to->layout->describe(to->layout));
        laik_log_flush("local off %lu (ptr %p) => %lu (ptr %p)",
            fromOff, fromPtr, toOff, toPtr);
    }

    for(int64_t i3 = 0; i3 < count.i[2]; i3++) {
        char *fromPtr2 = fromPtr;
        char *toPtr2 = toPtr;
        for(int64_t i2 = 0; i2 < count.i[1]; i2++) {
            memcpy(toPtr2, fromPtr2, count.i[0] * elemsize);
            fromPtr2 += fromLayout->stride[1] * elemsize;
            toPtr2   += toLayout->stride[1] * elemsize;
        }
        fromPtr += fromLayout->stride[2] * elemsize;
        toPtr   += toLayout->stride[2] * elemsize;
    }
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
unsigned int laik_pack_lex(Laik_Mapping* m, Laik_Slice* s,
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
    uint64_t idxOff = laik_offset(idx, m->layout);
    char* idxPtr = m->start + idxOff * elemsize;

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
unsigned int laik_unpack_lex(Laik_Mapping* m, Laik_Slice* s,
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
    uint64_t idxOff = laik_offset(idx, m->layout);
    char* idxPtr = m->start + idxOff * elemsize;

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

    sprintf(s, "lex %dd, strides %llu/%llu/%llu",
             l->dims,
             (unsigned long long) layout->stride[0],
             (unsigned long long) layout->stride[1],
             (unsigned long long) layout->stride[2]);

    return s;
}

// allocate new layout object for lexicographical layout
Laik_Layout* laik_new_layout_lex(Laik_Slice* slc)
{
    int dims = slc->space->dims;
    Laik_Layout_Lex* l = malloc(sizeof(Laik_Layout_Lex));
    if (!l) {
        laik_panic("Out of memory allocating Laik_Layout_Lex object");
        exit(1); // not actually needed, laik_panic never returns
    }
    laik_init_layout(&(l->h), dims, laik_slice_size(slc),
                     laik_pack_lex, laik_unpack_lex,
                     laik_layout_describe_lex, laik_offset_lex,
                     laik_layout_copy_lex,
                     laik_first_lex, laik_next_lex);

    l->slc = *slc;

    assert(slc->from.i[0] < slc->to.i[0]);
    l->stride[0] = 1;

    if (dims > 1) {
        l->stride[1] = slc->to.i[0] - slc->from.i[0];
        assert(slc->from.i[1] < slc->to.i[1]);
    }
    else
        l->stride[1] = 0; // invalid, not used

    if (dims > 2) {
        l->stride[2] = l->stride[1] * (slc->to.i[1] - slc->from.i[1]);
        assert(slc->from.i[2] < slc->to.i[2]);
    }
    else
        l->stride[2] = 0; // invalid, not used

    return (Laik_Layout*) l;
}


// return lex layout if given layout is a lexicographical layout
Laik_Layout_Lex* laik_is_layout_lex(Laik_Layout* l)
{
    if (l->offset == laik_offset_lex)
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

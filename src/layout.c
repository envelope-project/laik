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
bool next_lex(Laik_Range* range, Laik_Index* idx)
{
    idx->i[0]++;
    if (idx->i[0] < range->to.i[0]) return true;
    if (range->space->dims == 1) return false;

    idx->i[1]++;
    idx->i[0] = range->from.i[0];
    if (idx->i[1] < range->to.i[1]) return true;
    if (range->space->dims == 2) return false;

    idx->i[2]++;
    idx->i[1] = range->from.i[1];
    if (idx->i[2] < range->to.i[2]) return true;
    return false;
}

// generic copy just using offset function from layout interface
void laik_layout_copy_gen(Laik_Range* range,
                          Laik_Mapping* from, Laik_Mapping* to)
{
    Laik_Layout* fromLayout = from->layout;
    Laik_Layout* toLayout = to->layout;
    unsigned int elemsize = from->data->elemsize;
    assert(elemsize == to->data->elemsize);

    if (laik_log_begin(1)) {
        laik_log_append("generic copy of range ");
        laik_log_Range(range);
        laik_log_append(" (count %llu, elemsize %d) from mapping %p",
            laik_range_size(range), elemsize, from->start);
        laik_log_append(" (data '%s'/%d, %s) ",
            from->data->name, from->mapNo,
            from->layout->describe(from->layout));
        laik_log_flush("to mapping %p (data '%s'/%d, layout %s): ",
            to->start, to->data->name, to->mapNo,
            to->layout->describe(to->layout));
    }

    Laik_Index idx = range->from;
    uint64_t count = 0;
    do {
        int64_t fromOffset = fromLayout->offset(fromLayout, from->layoutSection, &idx);
        int64_t toOffset = toLayout->offset(toLayout, to->layoutSection, &idx);
        void* fromPtr = from->start + fromOffset * elemsize;
        void* toPtr = to->start + toOffset * elemsize;
#if 0
        if (laik_log_begin(1)) {
            laik_log_append(" copy idx ");
            laik_log_Index(range->space->dims, &idx);
            laik_log_flush(" from off %lu (ptr %p) to %lu (%p)",
                           fromOffset, fromPtr, toOffset, toPtr);
        }
#endif
        memcpy(toPtr, fromPtr, elemsize);
        count++;
    } while(next_lex(range, &idx));
    assert(count == laik_range_size(range));
}

// generic pack just using offset function from layout interface.
// this packs data according to lexicographical traversal
// return number of elements packed into provided buffer
unsigned int laik_layout_pack_gen(Laik_Mapping* m, Laik_Range* range,
                                  Laik_Index* idx, char* buf, unsigned int size)
{
    unsigned int elemsize = m->data->elemsize;
    Laik_Layout* layout = m->layout;
    int dims = m->layout->dims;

    if (laik_index_isEqual(dims, idx, &(range->to))) {
        // nothing left to pack
        return 0;
    }

    // range to pack must within local valid range of mapping
    assert(laik_range_within_range(range, &(m->requiredRange)));

    if (laik_log_begin(1)) {
        laik_log_append("        generic packing of range ");
        laik_log_Range(range);
        laik_log_append(" (count %llu, elemsize %d) from mapping %p",
            laik_range_size(range), elemsize, m->start);
        laik_log_append(" (data '%s'/%d, %s) at idx ",
            m->data->name, m->mapNo, layout->describe(layout));
        laik_log_Index(dims, idx);
        laik_log_flush(" into buf (size %d)", size);
    }

    unsigned int count = 0;
    while(size >= elemsize) {
        int64_t off = layout->offset(layout, m->layoutSection, idx);
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

        if (!next_lex(range, idx)) {
            *idx = range->to;
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
unsigned int laik_layout_unpack_gen(Laik_Mapping* m, Laik_Range* range,
                                    Laik_Index* idx, char* buf, unsigned int size)
{
    unsigned int elemsize = m->data->elemsize;
    Laik_Layout* layout = m->layout;
    int dims = m->layout->dims;

    // there should be something to unpack
    assert(size > 0);
    assert(!laik_index_isEqual(dims, idx, &(range->to)));

    // range to unpack into must be within local valid range of mapping
    assert(laik_range_within_range(range, &(m->requiredRange)));

    if (laik_log_begin(1)) {
        laik_log_append("        generic unpacking of range ");
        laik_log_Range(range);
        laik_log_append(" (count %llu, elemsize %d) into mapping %p",
            laik_range_size(range), elemsize, m->start);
        laik_log_append(" (data '%s'/%d, %s) at idx ",
            m->data->name, m->mapNo, layout->describe(layout));
        laik_log_Index(dims, idx);
        laik_log_flush(" from buf (size %d)", size);
    }

    unsigned int count = 0;
    while(size >= elemsize) {
        int64_t off = layout->offset(layout, m->layoutSection, idx);
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

        if (!next_lex(range, idx)) {
            *idx = range->to;
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

// placeholder for "describe" function of layout interface if not implemented
static
char* laik_layout_describe_gen(Laik_Layout* l)
{
    static char s[100];

    sprintf(s, "unspecified %dd", l->dims);
    return s;
}


// initialize generic members of a layout
void laik_init_layout(Laik_Layout* l, int dims, int map_count, uint64_t count,
                      laik_layout_section_t section,
                      laik_layout_mapno_t mapno,
                      laik_layout_offset_t offset,
                      laik_layout_reuse_t reuse,
                      laik_layout_describe_t describe,
                      laik_layout_pack_t pack,
                      laik_layout_unpack_t unpack,
                      laik_layout_copy_t copy)
{
    l->dims = dims;
    l->map_count = map_count;
    l->count = count;

    // the offset and mapno functions must be provided
    assert(offset != 0);
    assert(mapno != 0);

    // for testing, LAIK_LAYOUT_GENERIC enforces use of generic variants
    if (getenv("LAIK_LAYOUT_GENERIC")) {
        copy = 0;
        pack = 0;
        unpack = 0;
    }

    // pack/unpack/copy are optional. If not given, use generic versions
    if (!pack)   pack   = laik_layout_pack_gen;
    if (!unpack) unpack = laik_layout_unpack_gen;
    if (!copy)   copy   = laik_layout_copy_gen;

    // describe is optional
    if (!describe) describe = laik_layout_describe_gen;

    l->section = section;
    l->mapno = mapno;
    l->offset = offset;
    l->reuse = reuse;
    l->pack = pack;
    l->unpack = unpack;
    l->describe = describe;
    l->copy = copy;
}



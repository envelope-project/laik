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

#ifndef _LAIK_DATA_INTERNAL_H_
#define _LAIK_DATA_INTERNAL_H_

#ifndef _LAIK_INTERNAL_H_
#error "include laik-internal.h instead"
#endif

// already included - this is just for an IDE to know about LAIK types
#include "laik-internal.h"

// kinds of data types supported by Laik
typedef enum _Laik_TypeKind {
    LAIK_TK_None = 0,
    LAIK_TK_POD       // "Plain Old Data", just a sequence of bytes
} Laik_TypeKind;

// a data type
struct _Laik_Type {
    char* name;
    int id;

    Laik_TypeKind kind;
    int size;      // in bytes (for POD)

    // callbacks for reductions
    // initialize values of this type with neutral element
    laik_init_t init;
    // do a reduction on input arrays
    laik_reduce_t reduce;

    // callbacks for packing/unpacking
    int (*getLength)(Laik_Data*,Laik_Slice*);
    bool (*convert)(Laik_Data*,Laik_Slice*, void*);
};

// statistics for switching
struct _Laik_SwitchStat
{
    int switches, switches_noactions;
    int mallocCount, freeCount, sendCount, recvCount, reduceCount;
    uint64_t mallocedBytes, freedBytes, initedBytes, copiedBytes;
    uint64_t sentBytes, receivedBytes, reducedBytes;
};

Laik_SwitchStat* laik_newSwitchStat();
void laik_addSwitchStat(Laik_SwitchStat* target, Laik_SwitchStat* src);

// a data container
struct _Laik_Data {
    char* name;
    int id;

    int elemsize;
    Laik_Space* space; // index space of this container
    Laik_Group* group;
    Laik_Type* type;

    // active partitioning
    Laik_Partitioning* activePartitioning;
    Laik_DataFlow activeFlow;
    // linked list of data objects with same active partitioning
    Laik_Data* nextPartitioningUser;

    // active mappings (multiple possible, one per slice)
    Laik_MappingList* activeMappings;

    Laik_Allocator* allocator;

    // can be set by backend
    void* backend_data;

    // statistics
    Laik_SwitchStat* stat;
};

struct _Laik_Layout {
    Laik_LayoutType type;
    bool isFixed; // still variable, or fixed to a given layout
    int dims, order[3]; // at most 3 dimensions
    uint64_t stride[3];

    // pack data of slice in given mapping with this layout into <buf>,
    // using at most <size> bytes, starting at index <idx>.
    // called iteratively by backends, using <idx> to remember position
    // accross multiple calls. <idx> must be set first to index at beginning.
    // returns the number of elements written (or 0 if finished)
    int (*pack)(Laik_Mapping* m, Laik_Slice* s, Laik_Index* idx,
                char* buf, int size);

    // unpack data from <buf> with <size> bytes length into given slice of
    // memory space provided by mapping, incrementing index accordingly.
    // returns number of elements unpacked.
    int (*unpack)(Laik_Mapping* m, Laik_Slice* s, Laik_Index* idx,
                char* buf, int size);
};

// a mapping of data elements for global index range given by <validSlice>,
// with index <validSlice.from> mapped to address <base>.
// This may be embedded in a larger mapping for <fullSlice> at <start>
// with <fullcount> elements.
// Memory of the larger mapping space is kept for future reuse.
struct _Laik_Mapping {
    Laik_Data* data;
    int mapNo; // index of this map in local mapping list
    int firstOff, lastOff; // offsets in border array covered by this mapping
    Laik_Layout* layout; // memory layout used
    Laik_Slice allocatedSlice; // slice (global) covered by this mapping
    Laik_Slice requiredSlice; // sub-slice (global) containing used slices
    uint64_t count, allocCount; // number of elements in req/allcSlice
    uint64_t size[3];

    char* start; // start address of mapping
    char* base; // address matching requiredSlice.from (usually same as start)
    uint64_t capacity; // number of bytes allocated
    int reusedFor; // -1: not reused, otherwise map number used for
};

struct _Laik_MappingList {
    int count;
    Laik_Mapping map[1];
};

// initialize the LAIK data module, called from laik_new_instance
void laik_data_init();

// ensure that the mapping is backed by memory (called by backends)
void laik_allocateMap(Laik_Mapping* m, Laik_SwitchStat *ss);

#endif // _LAIK_DATA_INTERNAL_H_

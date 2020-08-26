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

#ifndef LAIK_DATA_INTERNAL_H
#define LAIK_DATA_INTERNAL_H

#include <laik.h>     // for Laik_Mapping, Laik_Slice, Laik_Data, Laik_Switc...
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uint64_t

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

Laik_Type* laik_type_new(char* name, Laik_TypeKind kind, int size,
                         laik_init_t init, laik_reduce_t reduce);

// statistics for switching
struct _Laik_SwitchStat
{
    int switches, switches_noactions;
    int mallocCount, freeCount;
    uint64_t mallocedBytes, freedBytes, initedBytes, copiedBytes;
    uint64_t currAllocedBytes, maxAllocedBytes;
    int transitionCount;
    unsigned int msgSendCount, msgRecvCount, msgReduceCount;
    unsigned int msgAsyncSendCount, msgAsyncRecvCount;
    uint64_t elemSendCount, elemRecvCount, elemReduceCount;
    uint64_t byteSendCount, byteRecvCount, byteReduceCount;
    uint64_t initOpCount, reduceOpCount, byteBufCopyCount;

};

Laik_SwitchStat* laik_newSwitchStat(void);
void laik_addSwitchStat(Laik_SwitchStat* target, Laik_SwitchStat* src);
void laik_switchstat_addASeq(Laik_SwitchStat* target, Laik_ActionSeq* as);
void laik_switchstat_malloc(Laik_SwitchStat* ss, uint64_t bytes);
void laik_switchstat_free(Laik_SwitchStat* ss, uint64_t bytes);

// information for a reservation
typedef struct _Laik_ReservationEntry {
    Laik_Partitioning* p;
    Laik_MappingList* mList; // from partitioning-local map numbers to mappings
} Laik_ReservationEntry;

struct _Laik_Reservation {
    int id;
    char* name;

    Laik_Data* data;

    // for reservations
    int count;    // number of partitionings registered for reservation
    int capacity; // number of entries allocated
    Laik_ReservationEntry* entry; // list of partitionings part of reservation
    Laik_MappingList* mList; // mappings for reservations
};

// a data container
struct _Laik_Data {
    char* name;
    int id;

    unsigned int elemsize;
    Laik_Space* space; // index space of this container
    Laik_Type* type;

    // currently used partitioning and data flow
    Laik_Partitioning* activePartitioning;

    // active mappings (multiple possible)
    Laik_MappingList* activeMappings;

    // when switching to a partitioning, we check for reservations first
    Laik_Reservation* activeReservation;

    Laik_Allocator* allocator;

    // can be set by backend
    void* backend_data;

    // statistics
    Laik_SwitchStat* stat;
};


// a layout defines order of indexes into memory
struct _Laik_Layout {
    int dims;
    laik_layout_pack_t pack;
    laik_layout_unpack_t unpack;
    laik_layout_describe_t describe;
    laik_layout_offset_t offset;
};

// lexicographical layout: 1d, 2d, 3d
struct _Laik_Layout_Lex {
    Laik_Layout h;

    uint64_t stride[3];
};

// a mapping of data elements for global index range given by <validSlice>,
// with index <validSlice.from> mapped to address <base>.
// This may be embedded in a larger mapping for <fullSlice> at <start>
// with <fullcount> elements.
// Memory of the larger mapping space is kept for future reuse.
struct _Laik_Mapping {
    Laik_Data* data;
    int mapNo; // index of this map in local mapping list
    Laik_Layout* layout; // memory layout used
    Laik_Slice allocatedSlice; // slice (global) covered by this mapping
    Laik_Slice requiredSlice; // sub-slice (global) containing used slices
    uint64_t count, allocCount; // number of elements in req/allcSlice

    char* start; // start address of mapping
    char* base; // address matching requiredSlice.from (usually same as start)
    uint64_t capacity; // number of bytes allocated
    int reusedFor; // -1: not reused, otherwise map number used for

    Laik_Allocator* allocator; // allocator to use when freeing the mapping
    Laik_Mapping* baseMapping; // mapping this one is embedded in
};

struct _Laik_MappingList {
    Laik_Reservation* res; // mappings belong to this reservation, may be 0
    int count;
    Laik_Mapping map[]; // a C99 "flexible array member"
};

// initialize the LAIK data module, called from laik_new_instance
void laik_data_init(void);

// create the types pre-provided by LAIK, to be called at data module init
void laik_type_init(void);

// create mapping descriptors without allocation for <n> maps for a container
Laik_MappingList* laik_mappinglist_new(Laik_Data* d, int n);

// ensure that the mapping is backed by memory (called by backends)
void laik_allocateMap(Laik_Mapping* m, Laik_SwitchStat *ss);

#endif // LAIK_DATA_INTERNAL_H

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

#include "laik/data.h"


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
    void (*init)(void* base, int count, Laik_DataFlow a);
    // do a reduction on input arrays
    void (*reduce)(void* out, void* in1, void* in2,
                   int count, Laik_DataFlow a);

    // callbacks for packing/unpacking
    int (*getLength)(Laik_Data*,Laik_Slice*);
    bool (*convert)(Laik_Data*,Laik_Slice*, void*);
};


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
};

struct _Laik_Layout {
    Laik_LayoutType type;
    bool isFixed; // still variable, or fixed to a given layout
    int dims, order[3]; // at most 3 dimensions
};

struct _Laik_Mapping {
    Laik_Data* data;
    int sliceNo; // slice number of own partition this mapping is for
    Laik_Layout* layout; // ordering layout used
    Laik_Index baseIdx; // global index at base address

    char* base; // start address of mapping
    int count; // number of elements mapped
};

struct _Laik_MappingList {
    int count;
    Laik_Mapping map[1];
};

// initialize the LAIK data module, called from laik_new_instance
void laik_data_init();

// ensure that the mapping is backed by memory (called by backends)
void laik_allocateMap(Laik_Mapping* m);

#endif // _LAIK_DATA_INTERNAL_H_

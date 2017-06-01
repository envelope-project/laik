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
    void (*init)(void* base, int count, Laik_AccessBehavior a);
    // do a reduction on input arrays
    void (*reduce)(void* out, void* in1, void* in2,
                   int count, Laik_AccessBehavior a);

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

    // default partitioning
    Laik_PartitionType defaultPartitionType;
    Laik_AccessBehavior defaultAccess;

    // active partitioning (TODO: multiple may be active)
    Laik_Partitioning* activePartitioning;
    Laik_Mapping* activeMapping;
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
    Laik_Partitioning* partitioning;
    int task; // slice/task number in partition
    Laik_Layout* layout; // ordering layout used
    Laik_Index baseIdx; // global index at base address

    char* base; // start address of mapping
    int count; // number of elements mapped
};

// initialize the LAIK data module, called from laik_new_instance
void laik_data_init();

//For repartitioning
void laik_set_partitioning_internal(Laik_Data* d, Laik_Partitioning* p, 
  int* failing);

#endif // _LAIK_DATA_INTERNAL_H_

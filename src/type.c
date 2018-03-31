/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 - 2018 LRR-TUM
 *               2017 - 2018 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
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

#include "laik-internal.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <float.h>

/**
 * LAIK data types
 *
 * This provides the implementation of LAIK-provided types.
 *
 * You can register your own custom data types with laik_type_register().
 * To allow reductions to be done on such types at switch time, you need
 * to also provide your own
 * - reduction function for various reduction operations
 * - initialization function with neutral element of a reduction operations
 * using laik_type_set_reduce/laik_type_set_init.
 */


// Provided types, directly usable in LAIK applications after laik_init_*()
Laik_Type *laik_Char;
Laik_Type *laik_Int32;
Laik_Type *laik_Int64;
Laik_Type *laik_UChar;
Laik_Type *laik_UInt32;
Laik_Type *laik_UInt64;
Laik_Type *laik_Float;
Laik_Type *laik_Double;

static int type_id = 0;

// laik_Char (signed)

void laik_char_init(void* base, int count, Laik_ReductionOperation o)
{
    signed char* p = base;
    signed char v;
    switch(o) {
    case LAIK_RO_Sum:
    case LAIK_RO_Or:   v = 0; break;
    case LAIK_RO_Prod: v = 1; break;
    case LAIK_RO_And:  v = ~0; break;
    case LAIK_RO_Min:  v = INT8_MIN; break;
    case LAIK_RO_Max:  v = INT8_MAX; break;
    default:
        assert(0);
    }
    for(int i = 0; i < count; i++)
        p[i] = v;
}

void laik_char_reduce(void* out, void* in1, void* in2,
                      int count, Laik_ReductionOperation o)
{
    assert(out);

    if (!in1 || !in2) {
        // for all supported reductions, only one input is copied as output
        if (in1)
            memcpy(out, in1, count * sizeof(char));
        else if (in2)
            memcpy(out, in2, count * sizeof(char));
        else
            laik_char_init(out, count, o);
        return;
    }

    signed char* pin1 = in1;
    signed char* pin2 = in2;
    signed char* pout = out;
    switch(o) {
    case LAIK_RO_Sum:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] + pin2[i];
        break;

    case LAIK_RO_Prod:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] * pin2[i];
        break;

    case LAIK_RO_Or:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] | pin2[i];
        break;

    case LAIK_RO_And:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] & pin2[i];
        break;

    case LAIK_RO_Min:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] < pin2[i]) ? pin1[i] : pin2[i];
        break;

    case LAIK_RO_Max:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] > pin2[i]) ? pin1[i] : pin2[i];
        break;

    default:
        assert(0);
    }
}


// laik_UChar

void laik_uchar_init(void* base, int count, Laik_ReductionOperation o)
{
    unsigned char* p = base;
    unsigned char v;
    switch(o) {
    case LAIK_RO_Sum:
    case LAIK_RO_Or:   v = 0; break;
    case LAIK_RO_Prod: v = 1; break;
    case LAIK_RO_And:  v = 255; break;
    case LAIK_RO_Min:  v = 0; break;
    case LAIK_RO_Max:  v = UINT8_MAX; break;
    default:
        assert(0);
    }
    for(int i = 0; i < count; i++)
        p[i] = v;
}

void laik_uchar_reduce(void* out, void* in1, void* in2,
                       int count, Laik_ReductionOperation o)
{
    assert(out);
    if (!in1 || !in2) {
        // for all supported reductions, only one input is copied as output
        if (in1)
            memcpy(out, in1, count * sizeof(unsigned char));
        else if (in2)
            memcpy(out, in2, count * sizeof(unsigned char));
        else
            laik_char_init(out, count, o);
        return;
    }

    unsigned char* pin1 = in1;
    unsigned char* pin2 = in2;
    unsigned char* pout = out;
    switch(o) {
    case LAIK_RO_Sum:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] + pin2[i];
        break;

    case LAIK_RO_Prod:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] * pin2[i];
        break;

    case LAIK_RO_Or:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] | pin2[i];
        break;

    case LAIK_RO_And:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] & pin2[i];
        break;

    case LAIK_RO_Min:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] < pin2[i]) ? pin1[i] : pin2[i];
        break;

    case LAIK_RO_Max:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] > pin2[i]) ? pin1[i] : pin2[i];
        break;

    default:
        assert(0);
    }
}


// laik_Int32 (signed)

void laik_int32_init(void* base, int count, Laik_ReductionOperation o)
{
    int32_t* p = base;
    int32_t v;
    switch(o) {
    case LAIK_RO_Sum:
    case LAIK_RO_Or:   v = 0; break;
    case LAIK_RO_Prod: v = 1; break;
    case LAIK_RO_And:  v = ~0; break;
    case LAIK_RO_Min:  v = INT32_MIN; break;
    case LAIK_RO_Max:  v = INT32_MAX; break;
    default:
        assert(0);
    }
    for(int i = 0; i < count; i++)
        p[i] = v;
}

void laik_int32_reduce(void* out, void* in1, void* in2,
                       int count, Laik_ReductionOperation o)
{
    assert(out);
    if (!in1 || !in2) {
        // for all supported reductions, only one input is copied as output
        if (in1)
            memcpy(out, in1, count * sizeof(int32_t));
        else if (in2)
            memcpy(out, in2, count * sizeof(int32_t));
        else
            laik_char_init(out, count, o);
        return;
    }

    int32_t* pin1 = in1;
    int32_t* pin2 = in2;
    int32_t* pout = out;
    switch(o) {
    case LAIK_RO_Sum:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] + pin2[i];
        break;

    case LAIK_RO_Prod:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] * pin2[i];
        break;

    case LAIK_RO_Or:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] | pin2[i];
        break;

    case LAIK_RO_And:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] & pin2[i];
        break;

    case LAIK_RO_Min:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] < pin2[i]) ? pin1[i] : pin2[i];
        break;

    case LAIK_RO_Max:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] > pin2[i]) ? pin1[i] : pin2[i];
        break;

    default:
        assert(0);
    }
}


// laik_UInt32

void laik_uint32_init(void* base, int count, Laik_ReductionOperation o)
{
    uint32_t* p = base;
    uint32_t v;
    switch(o) {
    case LAIK_RO_Sum:
    case LAIK_RO_Or:   v = 0; break;
    case LAIK_RO_Prod: v = 1; break;
    case LAIK_RO_And:  v = ~0; break;
    case LAIK_RO_Min:  v = 0; break;
    case LAIK_RO_Max:  v = UINT32_MAX; break;
    default:
        assert(0);
    }
    for(int i = 0; i < count; i++)
        p[i] = v;
}

void laik_uint32_reduce(void* out, void* in1, void* in2,
                        int count, Laik_ReductionOperation o)
{
    assert(out);
    if (!in1 || !in2) {
        // for all supported reductions, only one input is copied as output
        if (in1)
            memcpy(out, in1, count * sizeof(uint32_t));
        else if (in2)
            memcpy(out, in2, count * sizeof(uint32_t));
        else
            laik_char_init(out, count, o);
        return;
    }

    uint32_t* pin1 = in1;
    uint32_t* pin2 = in2;
    uint32_t* pout = out;
    switch(o) {
    case LAIK_RO_Sum:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] + pin2[i];
        break;

    case LAIK_RO_Prod:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] * pin2[i];
        break;

    case LAIK_RO_Or:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] | pin2[i];
        break;

    case LAIK_RO_And:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] & pin2[i];
        break;

    case LAIK_RO_Min:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] < pin2[i]) ? pin1[i] : pin2[i];
        break;

    case LAIK_RO_Max:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] > pin2[i]) ? pin1[i] : pin2[i];
        break;

    default:
        assert(0);
    }
}


// laik_Int64 (signed)

void laik_int64_init(void* base, int count, Laik_ReductionOperation o)
{
    int64_t* p = base;
    int64_t v;
    switch(o) {
    case LAIK_RO_Sum:
    case LAIK_RO_Or:   v = 0l; break;
    case LAIK_RO_Prod: v = 1l; break;
    case LAIK_RO_And:  v = ~0l; break;
    case LAIK_RO_Min:  v = INT64_MIN; break;
    case LAIK_RO_Max:  v = INT64_MAX; break;
    default:
        assert(0);
    }
    for(int i = 0; i < count; i++)
        p[i] = v;
}

void laik_int64_reduce(void* out, void* in1, void* in2,
                       int count, Laik_ReductionOperation o)
{
    assert(out);
    if (!in1 || !in2) {
        // for all supported reductions, only one input is copied as output
        if (in1)
            memcpy(out, in1, count * sizeof(int64_t));
        else if (in2)
            memcpy(out, in2, count * sizeof(int64_t));
        else
            laik_char_init(out, count, o);
        return;
    }

    int64_t* pin1 = in1;
    int64_t* pin2 = in2;
    int64_t* pout = out;
    switch(o) {
    case LAIK_RO_Sum:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] + pin2[i];
        break;

    case LAIK_RO_Prod:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] * pin2[i];
        break;

    case LAIK_RO_Or:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] | pin2[i];
        break;

    case LAIK_RO_And:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] & pin2[i];
        break;

    case LAIK_RO_Min:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] < pin2[i]) ? pin1[i] : pin2[i];
        break;

    case LAIK_RO_Max:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] > pin2[i]) ? pin1[i] : pin2[i];
        break;

    default:
        assert(0);
    }
}


// laik_UInt64

void laik_uint64_init(void* base, int count, Laik_ReductionOperation o)
{
    uint64_t* p = base;
    uint64_t v;
    switch(o) {
    case LAIK_RO_Sum:
    case LAIK_RO_Or:   v = 0l; break;
    case LAIK_RO_Prod: v = 1l; break;
    case LAIK_RO_And:  v = ~0l; break;
    case LAIK_RO_Min:  v = 0; break;
    case LAIK_RO_Max:  v = UINT64_MAX; break;
    default:
        assert(0);
    }
    for(int i = 0; i < count; i++)
        p[i] = v;
}

void laik_uint64_reduce(void* out, void* in1, void* in2,
                        int count, Laik_ReductionOperation o)
{
    assert(out);
    if (!in1 || !in2) {
        // for all supported reductions, only one input is copied as output
        if (in1)
            memcpy(out, in1, count * sizeof(uint64_t));
        else if (in2)
            memcpy(out, in2, count * sizeof(uint64_t));
        else
            laik_char_init(out, count, o);
        return;
    }

    uint64_t* pin1 = in1;
    uint64_t* pin2 = in2;
    uint64_t* pout = out;
    switch(o) {
    case LAIK_RO_Sum:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] + pin2[i];
        break;

    case LAIK_RO_Prod:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] * pin2[i];
        break;

    case LAIK_RO_Or:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] | pin2[i];
        break;

    case LAIK_RO_And:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] & pin2[i];
        break;

    case LAIK_RO_Min:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] < pin2[i]) ? pin1[i] : pin2[i];
        break;

    case LAIK_RO_Max:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] > pin2[i]) ? pin1[i] : pin2[i];
        break;

    default:
        assert(0);
    }
}


// laik_Double

void laik_double_init(void* base, int count, Laik_ReductionOperation o)
{
    double* p = base;
    double v;
    switch(o) {
    case LAIK_RO_Sum:  v = 0.0; break;
    case LAIK_RO_Prod: v = 1.0; break;
    case LAIK_RO_Min:  v = -DBL_MAX; break;
    case LAIK_RO_Max:  v = DBL_MAX; break;
    default:
        assert(0);
    }
    for(int i = 0; i < count; i++)
        p[i] = v;
}

void laik_double_reduce(void* out, void* in1, void* in2,
                        int count, Laik_ReductionOperation o)
{
    assert(out);
    if (!in1 || !in2) {
        // for all supported reductions, only one input is copied as output
        if (in1)
            memcpy(out, in1, count * sizeof(double));
        else if (in2)
            memcpy(out, in2, count * sizeof(double));
        else
            laik_double_init(out, count, o);
        return;
    }

    double* pin1 = in1;
    double* pin2 = in2;
    double* pout = out;
    switch(o) {
    case LAIK_RO_Sum:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] + pin2[i];
        break;

    case LAIK_RO_Prod:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] * pin2[i];
        break;

    case LAIK_RO_Min:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] < pin2[i]) ? pin1[i] : pin2[i];
        break;

    case LAIK_RO_Max:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] > pin2[i]) ? pin1[i] : pin2[i];
        break;

    default:
        assert(0);
    }
}


// laik_Float

void laik_float_init(void* base, int count, Laik_ReductionOperation o)
{
    float* p = base;
    float v;
    switch(o) {
    case LAIK_RO_Sum:  v = 0.0; break;
    case LAIK_RO_Prod: v = 1.0; break;
    case LAIK_RO_Min:  v = -FLT_MAX; break;
    case LAIK_RO_Max:  v = FLT_MAX; break;
    default:
        assert(0);
    }
    for(int i = 0; i < count; i++)
        p[i] = v;
}

void laik_float_reduce(void* out, void* in1, void* in2,
                       int count, Laik_ReductionOperation o)
{
    assert(out);
    if (!in1 || !in2) {
        // for all supported reductions, only one input is copied as output
        if (in1)
            memcpy(out, in1, count * sizeof(float));
        else if (in2)
            memcpy(out, in2, count * sizeof(float));
        else
            laik_float_init(out, count, o);
        return;
    }

    float* pin1 = in1;
    float* pin2 = in2;
    float* pout = out;
    switch(o) {
    case LAIK_RO_Sum:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] + pin2[i];
        break;

    case LAIK_RO_Prod:
        for(int i = 0; i < count; i++)
            pout[i] = pin1[i] * pin2[i];
        break;

    case LAIK_RO_Min:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] < pin2[i]) ? pin1[i] : pin2[i];
        break;

    case LAIK_RO_Max:
        for(int i = 0; i < count; i++)
            pout[i] = (pin1[i] > pin2[i]) ? pin1[i] : pin2[i];
        break;

    default:
        assert(0);
    }
}



Laik_Type* laik_type_new(char* name, Laik_TypeKind kind, int size,
                         laik_init_t init, laik_reduce_t reduce)
{
    Laik_Type* t = malloc(sizeof(Laik_Type));
    if (!t) {
        laik_panic("Out of memory allocating Laik_Type object");
        exit(1); // not actually needed, laik_panic never returns
    }

    t->id = type_id++;
    if (name)
        t->name = name;
    else {
        t->name = strdup("type-0     ");
        sprintf(t->name, "type-%d", t->id);
    }

    t->kind = kind;
    t->size = size;
    t->init = init;    // if 0: reductions not supported
    t->reduce = reduce;
    t->getLength = 0; // not needed for POD type
    t->convert = 0;

    return t;
}

Laik_Type* laik_type_register(char* name, int size)
{
    return laik_type_new(name, LAIK_TK_POD, size, 0, 0);
}

void laik_type_set_init(Laik_Type* type, laik_init_t init)
{
    type->init = init;
}

void laik_type_set_reduce(Laik_Type* type, laik_reduce_t reduce)
{
    type->reduce = reduce;
}


void laik_type_init()
{
    if (type_id > 0) return;

    laik_Char   = laik_type_new("char",  LAIK_TK_POD, 1,
                                laik_char_init, laik_char_reduce);
    laik_Int32  = laik_type_new("int32", LAIK_TK_POD, 4,
                                laik_int32_init, laik_int32_reduce);
    laik_Int64  = laik_type_new("int64", LAIK_TK_POD, 8,
                                laik_int64_init, laik_int64_reduce);
    laik_UChar   = laik_type_new("uchar",  LAIK_TK_POD, 1,
                                 laik_uchar_init, laik_uchar_reduce);
    laik_UInt32  = laik_type_new("uint32", LAIK_TK_POD, 4,
                                 laik_uint32_init, laik_uint32_reduce);
    laik_UInt64  = laik_type_new("uint64", LAIK_TK_POD, 8,
                                 laik_uint64_init, laik_uint64_reduce);
    laik_Float  = laik_type_new("float", LAIK_TK_POD, 4,
                                laik_float_init, laik_float_reduce);
    laik_Double = laik_type_new("double", LAIK_TK_POD, 8,
                                laik_double_init, laik_double_reduce);
}


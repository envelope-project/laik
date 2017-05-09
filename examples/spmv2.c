/* This file is part of the LAIK parallel container library.
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
 * SPMV example.
 */

#include "laik.h"

#ifdef USE_MPI
#include "laik-backend-mpi.h"
#else
#include "laik-backend-single.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>


//----------------------------------------------------------------------
// sparse matrix in CSR format

typedef struct _SpM SpM;
struct _SpM {
    int rows, cols;
    int elems;
    int* col;
    double* val;
    int row[1]; // variable size
};

// generate an (somehow arbitrary) triangular matrix in CSR format
SpM* newSpM(int size)
{
    SpM* m = (SpM*) malloc(sizeof(SpM) + size * sizeof(int));
    m->rows  = size;
    m->cols  = size;
    m->elems = (m->rows - 1) * m->cols / 2;
    m->col   = (int*) malloc(m->elems * sizeof(int));
    m->val   = (double*) malloc(m->elems * sizeof(double));

    int r, off = 0;
    for(r = 0; r < size; r++) {
        m->row[r] = off;
        for(int c = 0; c < r; c++) {
            m->col[off] = c;
            m->val[off] = (double) (size - r);
            off++;
        }
    }
    m->row[r] = off;
    assert(m->elems == off);

    return m;
}


// Callback for LAIK to allow a nice partitioning of a matrix by rows
//
// We use a 1d LAIK space to cover the rows of the sparse matrix, and we want
// a partition (ie. a range of rows) to roughly cover the same number of
// non-zero elements.
// To this end, use LAIK's support for element-wise weighted block partitioning
// by returning a weight for each row that is the number of non-zero elements
// in this row.
// This callback is configured to be used in the block paritioning
double getEW(Laik_Index* i, void* d)
{
    SpM* m = (SpM*) d;
    int ii = i->i[0];

    return (double) (m->row[ii + 1] - m->row[ii]);
}


//----------------------------------------------------------------------
// main

int main(int argc, char* argv[])
{
#ifdef USE_MPI
    Laik_Instance* inst = laik_init_mpi(&argc, &argv);
#else
    Laik_Instance* inst = laik_init_single();
#endif
    Laik_Group* world = laik_world(inst);

    // command line args: spmv [<maxiter> [<size>]] (def: spmv 10 10000)
    int maxiter = 0, size = 0;
    if (argc > 1) maxiter = atoi(argv[1]);
    if (argc > 2) size = atoi(argv[2]);
    if (maxiter == 0) maxiter = 10;
    if (size == 0) size = 10000;

    // generate a sparse matrix
    SpM* m = newSpM(size);

    // 1d space to partition matrix rows and result vector
    Laik_Space* s = laik_new_space_1d(inst, size);
    // LAIK container for result vector
    Laik_Data* resD = laik_alloc(world, s, laik_Double);
    // LAIK container for input vector
    Laik_Data* inpD = laik_alloc_1d(world, laik_Double, size);
    // for global normalization, to broadcast a vector sum to all
    Laik_Data* sumD = laik_alloc_1d(world, laik_Double, 1);

    // block partitioning according to number of non-zero elems in matrix rows
    Laik_Partitioning* p;
    p = laik_new_base_partitioning(s, LAIK_PT_Block, LAIK_AB_ReadWrite);
    laik_set_index_weight(p, getEW, m);
    laik_set_partitioning(resD, p);

    double *inp, *res, sum, *sumPtr;
    uint64_t icount, rcount, i;
    Laik_Slice* slc;
    int fromRow, toRow;

    // initialize input vector at master, broadcast to all
    laik_set_new_partitioning(inpD, LAIK_PT_Master, LAIK_AB_WriteAll);
    laik_map_def1(inpD, (void**) &inp, &icount);
    for(i = 0; i < icount; i++) inp[i] = 1.0;

    // do a sequence of SpMV, starting with v as input vector,
    // normalize result after each step to use as input for the next round
    for(int iter = 0; iter < maxiter; iter++) {

        // access to complete input vector (local indexing = global indexing)
        laik_set_new_partitioning(inpD, LAIK_PT_All, LAIK_AB_ReadOnly);
        laik_map_def1(inpD, (void**) &inp, 0);
        // ensure access to my partition of result vector (local indexing, from 0)
        laik_map_def1(resD, (void**) &res, &rcount);

        // zero out result vector (only my partition)
        for(i = 0; i < rcount; i++) res[i] = 0.0;

        // SpMV operation, for my range of rows
        slc = laik_my_slice(p);
        fromRow = slc->from.i[0];
        toRow = slc->to.i[0];
        for(int r = fromRow; r < toRow; r++) {
            for(int o = m->row[r]; o < m->row[r+1]; o++)
                res[r - fromRow] += m->val[o] * inp[m->col[o]];
        }

        // partitial sum of result
        sum = 0.0;
        for(i = 0; i < rcount; i++) sum += res[i];

        // compute global sum with LAIK, broadcast result to all
        laik_set_new_partitioning(sumD, LAIK_PT_All, LAIK_AB_WASum);
        laik_map_def1(sumD, (void**) &sumPtr, 0);
        *sumPtr = sum;
        laik_set_new_partitioning(sumD, LAIK_PT_All, LAIK_AB_ReadOnly);
        laik_map_def1(sumD, (void**) &sumPtr, 0);
        sum = *sumPtr;

        if (laik_myid(world) == 0) {
            printf("Sum at iter %2d: %f\n", iter, sum);
        }

        // make input vector writable, broadcast via sum reduction
        laik_set_new_partitioning(inpD, LAIK_PT_All, LAIK_AB_Sum);
        laik_map_def1(inpD, (void**) &inp, 0);
        // normalize values from my partition of result vector into next input
        for(i = 0; i < rcount; i++) inp[i + fromRow] = res[i] / sum;

        // react on repartitioning wishes
        //allowRepartitioning(p);
    }

    // push result to master
    laik_set_new_partitioning(inpD, LAIK_PT_Master, LAIK_AB_ReadOnly);
    laik_set_new_partitioning(resD, LAIK_PT_Master, LAIK_AB_ReadOnly);
    if (laik_myid(world) == 0) {
        double sum = 0.0;
        laik_map_def1(resD, (void**) &res, &rcount);
        for(i = 0; i < rcount; i++) sum += res[i];
        printf("Result sum: %f (should be same as last iter sum)\n", sum);
        laik_map_def1(inpD, (void**) &inp, &icount);
        sum = 0.0;
        for(i = 0; i < icount; i++) sum += inp[i];
        printf("Input sum: %f (should be 1.0)\n", sum);
    }

    laik_finalize(inst);
    return 0;
}

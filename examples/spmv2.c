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
#include <sys/time.h>

double wtime()
{
  struct timeval tv;
  gettimeofday(&tv, 0);

  return tv.tv_sec+1e-6*tv.tv_usec;
}

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
double getEW(Laik_Index* i, const void* d)
{
    SpM* m = (SpM*) d;
    int ii = i->i[0];

    return (double) (m->row[ii + 1] - m->row[ii]);
}


//----------------------------------------------------------------------
// main

void help(char* err)
{
    if (err)
        printf("Error parsing command line: %s\n", err);
    printf("Usage: (nmpirun ...) spmv2 [options] [<itercount> [<size>]]\n\n");
    printf("Arguments:\n"
           " <itercount>     number of iterations to do (def: 10)\n"
           " <size>          side length of sparse matrix (def: 10000)\n");
    printf("Options:\n"
           " -h              show this help and exit\n"
           " -r              use all-reduction to aggregate result (def: block+copy)\n"
           " -s <iter>       iterations after which to shrink by removing task 0\n"
           " -t <task>       on shrinking, remove task with ID <task> (default 0)\n"
           " -i              use incremental partitioner on shrinking\n"
           " -v              make LAIK verbose (same as LAIK_LOG=1)\n");
    exit(1);
}

int main(int argc, char* argv[])
{
#ifdef USE_MPI
    Laik_Instance* inst = laik_init_mpi(&argc, &argv);
#else
    Laik_Instance* inst = laik_init_single();
#endif
    Laik_Group* world = laik_world(inst);

    // command line args: spmv [<maxiter> [<size>]] (def: spmv 10 10000)
    int maxiter = 0, size = 0, nextshrink = -1, shrink = -1, removeTask = 0;
    bool useReduction = false;
    bool useIncremental = false;

    // timing: t1 raw computation, t2: everything without init
    double t1 = 0.0, t2 = 0.0, tt1, tt2, tt;

    // better debug output
    laik_set_phase(inst, 0, "init", NULL);

    int arg = 1, argno = 0;
    while(arg < argc) {
        if (argv[arg][0] == '-') {
            if (argv[arg][1] == 'r')
                useReduction = true;
            else if (argv[arg][1] == 'i')
                useIncremental = true;
            else if (argv[arg][1] == 'v')
                laik_set_loglevel(1);
            else if (argv[arg][1] == 's') {
                arg++;
                if (arg < argc)
                    nextshrink = shrink = atoi(argv[arg]);
                else
                    help("-s: no parameter");
            }
            else if (argv[arg][1] == 't') {
                arg++;
                if (arg < argc)
                    removeTask = atoi(argv[arg]);
                else
                    help("-t: no parameter");
            }
            else if (argv[arg][1] == 'h')
                help(0);
            else
                help("unknown option");
        }
        else { // regular arguments
            argno++;
            if (argno == 1)
                maxiter = atoi(argv[arg]);
            else if (argno == 2)
                size = atoi(argv[arg]);
            else {
                help("too many arguments");
            }
        }
        arg++;
    }
    if (maxiter == 0) maxiter = 10;
    if (size == 0) size = 10000;

    laik_enable_profiling(inst);

    if (laik_myid(world) == 0) {
        printf("Running %d iterations, SpM side %d (~%.1f MB), %d tasks\n",
               maxiter, size, .000001*size*size*6, laik_size(world));
    }
    tt = wtime();

    // generate a sparse matrix
    SpM* m = newSpM(size);

    tt2 = wtime();
    laik_log(2, "Init done (%.3fs)\n", tt2 - tt);

    // 1d space to partition matrix rows and result vector
    Laik_Space* s = laik_new_space_1d(inst, size);
    // LAIK container for result vector
    Laik_Data* resD = laik_alloc(world, s, laik_Double);
    laik_data_set_name(resD, "result");
    // LAIK container for input vector
    Laik_Data* inpD = laik_alloc(world, s, laik_Double);
    laik_data_set_name(inpD, "input");
    // for global normalization, to broadcast a vector sum to all
    Laik_Data* sumD = laik_alloc_1d(world, laik_Double, 1);
    laik_data_set_name(sumD, "sum");
    laik_switchto_new(sumD, laik_All, LAIK_DF_None);

    // block partitioning according to number of non-zero elems in matrix rows
    Laik_Partitioner* pr = laik_new_block_partitioner(0, 1, getEW, 0, m);
    laik_set_index_weight(pr, getEW, m);
    Laik_Partitioning* p = laik_new_partitioning(world, s, pr, 0);
    // nothing to preserve between iterations (assume at least one iter)
    laik_switchto(resD, p, LAIK_DF_None);

    // partitionings for all task taking part in calculation
    Laik_Partitioning* allVec = laik_new_partitioning(world, s, laik_All, 0);

    double *inp, *res, sum, *sumPtr;
    uint64_t icount, rcount, i, fromRow, toRow;

    // initialize input vector at master, broadcast to all
    laik_switchto_new(inpD, laik_Master, LAIK_DF_CopyOut);
    laik_map_def1(inpD, (void**) &inp, &icount);
    for(i = 0; i < icount; i++) inp[i] = 1.0;

    // better debug output
    laik_set_phase(inst, 1, "SpmV", NULL);

    // do a sequence of SpMV, starting with v as input vector,
    // normalize result after each step to use as input for the next round
    for(int iter = 0; iter < maxiter; iter++) {

        // better debug output
        laik_set_iteration(inst, iter);

        // flow for result: only at last iteration, copy out
        if (iter + 1 == maxiter)
            laik_switchto(resD, p, LAIK_DF_CopyOut);

        // access to complete input vector (local indexing = global indexing)
        laik_switchto(inpD, allVec, LAIK_DF_CopyIn);
        laik_map_def1(inpD, (void**) &inp, 0);

        // SpMV operation, for my range of rows

        // do a partial sum of result during traversal
        sum = 0.0;
        tt1 = wtime();

        // loop over all local slices
        for(int sNo = 0; ; sNo++) {
            if (!laik_my_slice1(p, sNo, &fromRow, &toRow)) break;

            // my partition slice of result vector (local indexing, from 0)
            laik_map_def(resD, sNo, (void**) &res, &rcount);

#pragma omp parallel for schedule(dynamic,50)
            for(uint64_t r = fromRow; r < toRow; r++) {
                res[r - fromRow] = 0.0;
                for(int o = m->row[r]; o < m->row[r+1]; o++)
                    res[r - fromRow] += m->val[o] * inp[m->col[o]];
            }

            for(i = 0; i < rcount; i++)
                sum += res[i];
        }
        t1 += wtime() - tt1;

        // compute global sum with LAIK, broadcast result to all
        // only done by tasks which still take part in SPMV
        assert(laik_myid(laik_get_dgroup(sumD)) >= 0);

        laik_switchto_flow(sumD, LAIK_DF_ReduceOut | LAIK_DF_Sum);
        laik_map_def1(sumD, (void**) &sumPtr, 0);
        *sumPtr = sum;
        laik_switchto_flow(sumD, LAIK_DF_CopyIn);
        laik_map_def1(sumD, (void**) &sumPtr, 0);
        sum = *sumPtr;

        if (laik_myid(laik_get_dgroup(sumD)) == 0) {
            printf("Sum at iter %2d: %f\n", iter, sum);
        }

        tt = wtime();
        t2 += tt - tt2;
        tt2 = tt;
        laik_log(2, "Timing: %.3fs comp / %.3fs total\n", t1, t2);

        // scale owns results by global sum and write into input partitions
        if (useReduction) {
            // varian 1: broadcast written input values via sum reduction
            // makes input vector writable for all, triggers (unneeded) initialization
            laik_switchto(inpD, allVec,
                          LAIK_DF_Init | LAIK_DF_ReduceOut | LAIK_DF_Sum);
            laik_map_def1(inpD, (void**) &inp, 0);

            // loop over all local slices of result vector
            for(int sNo = 0; ; sNo++) {
                if (!laik_my_slice1(p, sNo, &fromRow, &toRow)) break;

                laik_map_def(resD, sNo, (void**) &res, &rcount);
                for(i = 0; i < rcount; i++) inp[i + fromRow] = res[i] / sum;
            }
        }
        else {
            // variant 2: broadcast written input values directly
            laik_switchto(inpD, p, LAIK_DF_CopyOut);
            // loop over all local slices of result and input vector
            for(int sNo = 0; laik_my_slice(p, sNo) != 0; sNo++) {
                laik_map_def(resD, sNo, (void**) &res, &rcount);
                laik_map_def(inpD, sNo, (void**) &inp, 0);
                for(i = 0; i < rcount; i++) inp[i] = res[i] / sum;
            }
        }

        // test task shrinking
        // remove task <removeTask> from all used partitionings
        Laik_Group* g = laik_get_pgroup(p);
        if ((iter == nextshrink) &&
            (laik_size(g) > 1) && (laik_size(g) >= removeTask)) {
            int removeList[1] = {removeTask};
            laik_log(2, "Shrinking: remove task %d (orig size %d)",
                     removeTask, laik_size(g));
            Laik_Group* g2 = laik_new_shrinked_group(g, 1, removeList);

            Laik_Partitioner* pr = 0;
            if (useIncremental)
                pr = laik_new_reassign_partitioner(g2, getEW, m);

            laik_migrate_and_repartition(p, g2, pr);
            laik_migrate_and_repartition(allVec, g2, 0);
            laik_migrate_and_repartition(laik_get_active(sumD), g2, 0);

            // TODO: replace world with g2
            nextshrink += shrink;

            if (laik_myid(g2) == -1) break;
        }
    }
    
    laik_iter_reset(inst);
    laik_set_phase(inst, 2, "post-proc", NULL);

    // push result to master
    laik_switchto_new(inpD, laik_Master, LAIK_DF_CopyIn);
    laik_switchto_new(resD, laik_Master, LAIK_DF_CopyIn);
    if (laik_myid(laik_get_dgroup(inpD)) == 0) {
        double sum = 0.0;
        laik_map_def1(resD, (void**) &res, &rcount);
        for(i = 0; i < rcount; i++) sum += res[i];
        printf("Result sum: %f (should be same as last iter sum)\n", sum);
        laik_map_def1(inpD, (void**) &inp, &icount);
        sum = 0.0;
        for(i = 0; i < icount; i++) sum += inp[i];
        printf("Input sum: %f (should be 1.0)\n", sum);
    }

    t2 += wtime() - tt2;
    laik_log(2, "Timing: %.3fs comp/iter, %.3fs total/iter, %.3fs total\n",
             t1 / maxiter, t2 / maxiter, t2);

    laik_log(2, "Timing: Laik total: %.3fs, backend: %.3fs\n",
             laik_get_total_time(), laik_get_backend_time());

    laik_finalize(inst);
    return 0;
}

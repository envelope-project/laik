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

#include <laik.h>

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
    SpM* m = malloc(sizeof(SpM) + size * sizeof(int));
    m->rows  = size;
    m->cols  = size;
    m->elems = (m->rows - 1) * m->cols / 2;
    m->col   = malloc(sizeof(int) * m->elems);
    m->val   = malloc(sizeof(double) * m->elems);

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
    Laik_Instance* inst = laik_init(&argc, &argv);
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

    // generate a sparse matrix: globally available in every process
    // not managed by LAIK, but SpMV work distribution from LAIK partitioning
    SpM* m = newSpM(size);

    tt2 = wtime();
    laik_log(2, "Init done (%.3fs)\n", tt2 - tt);

    // 1d space to partition matrix rows and result vector
    Laik_Space* s = laik_new_space_1d(inst, size);
    // LAIK container for result vector
    Laik_Data* resD = laik_new_data(s, laik_Double);
    laik_data_set_name(resD, "result");
    // LAIK container for input vector
    Laik_Data* inpD = laik_new_data(s, laik_Double);
    laik_data_set_name(inpD, "input");
    // for global normalization, to broadcast a vector sum to all
    Laik_Data* sumD = laik_new_data_1d(inst, laik_Double, 1);
    laik_data_set_name(sumD, "sum");
    laik_switchto_new_partitioning(sumD, world, laik_All, LAIK_DF_None, LAIK_RO_None);

    // block partitioning according to number of non-zero elems in matrix rows
    Laik_Partitioner* pr = laik_new_block_partitioner(0, 1, getEW, 0, m);
    laik_set_index_weight(pr, getEW, m);
    Laik_Partitioning* p = laik_new_partitioning(pr, world, s, 0);
    // nothing to preserve between iterations (assume at least one iter)
    laik_switchto_partitioning(resD, p, LAIK_DF_None, LAIK_RO_None);

    // partitionings for all tasks taking part in calculation
    Laik_Partitioning* pAll = laik_new_partitioning(laik_All, world, s, 0);

    double *inp, *res, sum, *sumPtr;
    uint64_t icount, rcount, i;
    int64_t fromRow, toRow;

    // initialize input vector at master, broadcast to all
    laik_switchto_new_partitioning(inpD, world, laik_Master,
                                   LAIK_DF_None, LAIK_RO_None);
    laik_get_map_1d(inpD, 0, (void**) &inp, &icount);
    for(i = 0; i < icount; i++) inp[i] = 1.0;

    // better debug output
    laik_set_phase(inst, 1, "SpmV", NULL);

    // broadcast complete input vector
    laik_switchto_partitioning(inpD, pAll, LAIK_DF_Preserve, LAIK_RO_None);

    // do a sequence of SpMV, starting with v as input vector,
    // normalize result after each step to use as input for the next round
    for(int iter = 0; iter < maxiter; iter++) {

        // better debug output
        laik_set_iteration(inst, iter);

        // access to complete input vector (local indexing = global indexing)
        laik_get_map_1d(inpD, 0, (void**) &inp, 0);

        // SpMV operation, for my range of rows

        // do a partial sum of result during traversal
        sum = 0.0;
        tt1 = wtime();

        // loop over all local ranges
        for(int rangeNo = 0; ; rangeNo++) {
            if (!laik_my_range_1d(p, rangeNo, &fromRow, &toRow)) break;

            // my partition range of result vector (local indexing, from 0)
            laik_get_map_1d(resD, rangeNo, (void**) &res, &rcount);

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic,50)
#endif
            for(int64_t r = fromRow; r < toRow; r++) {
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
        assert(laik_myid(laik_data_get_group(sumD)) >= 0);

        laik_switchto_flow(sumD, LAIK_DF_None, LAIK_RO_None);
        laik_get_map_1d(sumD, 0, (void**) &sumPtr, 0);
        *sumPtr = sum;
        laik_switchto_flow(sumD, LAIK_DF_Preserve, LAIK_RO_Sum);
        laik_get_map_1d(sumD, 0, (void**) &sumPtr, 0);
        sum = *sumPtr;

        if (laik_myid(laik_data_get_group(sumD)) == 0) {
            printf("Sum at iter %2d: %f\n", iter, sum);
        }

        tt = wtime();
        t2 += tt - tt2;
        tt2 = tt;
        laik_log(2, "Timing: %.3fs comp / %.3fs total\n", t1, t2);

        // scale owns results by global sum and write into input partitions
        if (useReduction) {
            // variant 1: broadcast written input values via sum reduction
            // makes input vector writable for all, triggers (unneeded) initialization
            laik_switchto_partitioning(inpD, pAll, LAIK_DF_Init, LAIK_RO_Sum);
            laik_get_map_1d(inpD, 0, (void**) &inp, 0);

            // loop over all local ranges of result vector
            for(int sNo = 0; ; sNo++) {
                if (!laik_my_range_1d(p, sNo, &fromRow, &toRow)) break;

                laik_get_map_1d(resD, sNo, (void**) &res, &rcount);
                for(i = 0; i < rcount; i++) inp[i + fromRow] = res[i] / sum;
            }
        }
        else {
            // variant 2: broadcast written input values directly
            laik_switchto_partitioning(inpD, p, LAIK_DF_None, LAIK_RO_None);
            // loop over all local ranges of result and input vector
            for(int rangeNo = 0; laik_my_range(p, rangeNo) != 0; rangeNo++) {
                laik_get_map_1d(resD, rangeNo, (void**) &res, &rcount);
                laik_get_map_1d(inpD, rangeNo, (void**) &inp, 0);
                for(i = 0; i < rcount; i++) inp[i] = res[i] / sum;
            }
        }

        // test task shrinking
        // remove task <removeTask> from all used partitionings
        Laik_Group* g = laik_partitioning_get_group(p);
        bool doShrink = (iter == nextshrink) && (iter +1 < maxiter) &&
                        (laik_size(g) > 1) &&
                        (laik_size(g) >= removeTask);
        if (doShrink) {
            nextshrink += shrink;

            int removeList[1] = {removeTask};
            laik_log(2, "Shrinking: remove task %d (orig size %d)",
                     removeTask, laik_size(g));
            Laik_Group* g2 = laik_new_shrinked_group(g, 1, removeList);
            Laik_Partitioning *pAll2, *pSumAll2, *p2;

            // adjust partitioning
            // - p  + alloc for resD (no need to preserve data)
            // - pAll + data for inpD (preserve)
            // - pSumAll for sumD (no need to preserve data)

            // will be used before end of iteration to switch inpD to
            pAll2 = laik_new_partitioning(laik_All, g2, s, 0);

            pSumAll2 = laik_new_partitioning(laik_All, g2,
                                             laik_data_get_space(sumD), 0);
            laik_switchto_partitioning(sumD, pSumAll2, LAIK_DF_None, LAIK_RO_None);

            if (useIncremental) {
                pr = laik_new_reassign_partitioner(g2, getEW, m);
                // this still generates a partitioning on <g>, which...
                p2 = laik_new_partitioning(pr, g, s, p);
                // ... can be migrated to be valid for <g2>
                laik_partitioning_migrate(p2, g2);
            }
            else
                p2 = laik_new_partitioning(pr, g2, s, 0);
            laik_switchto_partitioning(resD, p2, LAIK_DF_None, LAIK_RO_None);

            // TODO: use ref-counters and decrement
            pAll = pAll2;
            p = p2;
            world = g2;
        }
        // broadcast complete input vector
        laik_switchto_partitioning(inpD, pAll, LAIK_DF_Preserve, LAIK_RO_Sum);

        if (laik_myid(world) == -1) break;
    }
    
    laik_iter_reset(inst);
    laik_set_phase(inst, 2, "post-proc", NULL);

    laik_switchto_new_partitioning(resD, laik_data_get_group(resD),
                                   laik_Master, LAIK_DF_Preserve, LAIK_RO_None);
    if (laik_myid(laik_data_get_group(inpD)) == 0) {
        double sum = 0.0;
        laik_get_map_1d(resD, 0, (void**) &res, &rcount);
        for(i = 0; i < rcount; i++) sum += res[i];
        printf("Result sum: %f (should be same as last iter sum)\n", sum);
        laik_get_map_1d(inpD, 0, (void**) &inp, &icount);
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

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
 * 1d Jacobi example.
 */

#ifdef USE_MPI
#include "laik-backend-mpi.h"
#else
#include "laik-backend-single.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// fixed boundary values
double loValue = -5.0, hiValue = 10.0;

int main(int argc, char* argv[])
{
#ifdef USE_MPI
    Laik_Instance* inst = laik_init_mpi(&argc, &argv);
#else
    Laik_Instance* inst = laik_init_single();
#endif
    Laik_Group* world = laik_world(inst);

    int msize = 0;
    int maxiter = 0;
    if (argc > 1) msize = atoi(argv[1]);
    if (argc > 2) maxiter = atoi(argv[2]);

    if (msize == 0) msize = 10; // 10 mio entries
    if (maxiter == 0) maxiter = 50;

    if (laik_myid(world) == 0) {
        printf("%dm cells (mem %.1f MB), running %d iterations with %d tasks\n",
               msize,
               16.0 * msize,
               maxiter, laik_size(world));
    }

    uint64_t size = (uint64_t) msize * 1000000;

    double *baseR, *baseW, *sumPtr;
    uint64_t countR, countW, off, from, to;

    // two 1d arrays for jacobi, using same space
    Laik_Space* space = laik_new_space_1d(inst, size);
    Laik_Data* data1 = laik_alloc(world, space, laik_Double);
    Laik_Data* data2 = laik_alloc(world, space, laik_Double);

    // two partitionings:
    // - pWrite: distributes the cells to update
    // - pRead : extends pWrite partitions to allow reading neighbor values
    // partitionings are assigned to either data1/data2, exchanged after
    // every iteration
    Laik_Partitioning *pWrite, *pRead;
    pWrite = laik_new_partitioning(world, space,
                                  laik_new_block_partitioner1(), 0);
    // this extends pWrite partitions at borders by 1 index on inner borders
    // (the coupling is dynamic: any change in pWrite changes pRead)
    pRead = laik_new_partitioning(world, space,
                                  laik_new_halo_partitioner(1), pWrite);

    // for global sum, used for residuum and value sum at end
    Laik_Data* sumD = laik_alloc_1d(world, laik_Double, 1);
    laik_data_set_name(sumD, "sum");
    laik_switchto_new(sumD, laik_All, LAIK_DF_None);

    // start with writing (= initialization) data1
    Laik_Data* dWrite = data1;
    Laik_Data* dRead = data2;

    // distributed initialization
    laik_switchto(dWrite, pWrite, LAIK_DF_CopyOut);
    laik_map_def1(dWrite, (void**) &baseW, &countW);
    for(uint64_t i = 0; i < countW; i++) baseW[i] = (double) 0.0;
    // set fixed boundary values
    if (laik_global2local1(dWrite, 0, &off)) {
        // if global index 0 is local, it must be at local index 0
        assert(off == 0);
        baseW[off] = loValue;
    }
    if (laik_global2local1(dWrite, size-1, &off)) {
        // if last global index is local, it must be at countW-1
        assert(off == countW - 1);
        baseW[off] = hiValue;
    }
    laik_log(2, "Init done\n");

    // for statistics (with LAIK_LOG=2)
    double t, t1 = laik_wtime(), t2 = t1;
    int last_iter = 0;
    int res_iters = 0; // iterations done with residuum calculation

    int iter = 0;
    for(; iter < maxiter; iter++) {
        laik_set_iteration(inst, iter);

        // switch roles: data written before now is read
        if (dRead == data1) { dRead = data2; dWrite = data1; }
        else                { dRead = data1; dWrite = data2; }

        laik_switchto(dRead,  pRead,  LAIK_DF_CopyIn);
        laik_switchto(dWrite, pWrite, LAIK_DF_CopyOut);
        laik_map_def1(dRead,  (void**) &baseR, &countR);
        laik_map_def1(dWrite, (void**) &baseW, &countW);

        // local range for which to do 1d stencil, adjust at borders
        from = 0;
        to = countW;
        if (laik_global2local1(dWrite, 0, &off)) {
            // global index 0 is local
            assert(off == 0);
            baseW[off] = loValue;
            from++;
        }
        else {
            // start at inner border: adjust baseR such that
            //  baseR[i] and baseW[i] correspond to same global index
            assert(laik_local2global1(dWrite, 0) ==
                   laik_local2global1(dRead, 0) + 1);
            baseR++;
        }
        if (laik_global2local1(dWrite, size-1, &off)) {
            // last global index is local
            assert(off == countW - 1);
            baseW[off] = hiValue;
            to--;
        }

        // do jacobi

        // check for residuum every 5 iterations
        if ((iter % 10) == 0) {
            // using work load in all tasks
            double newValue, diff, res;
            res = 0.0;
            for(uint64_t i = from; i < to; i++) {
                newValue = 0.5 * (baseR[i-1] + baseR[i+1]);
                diff = baseR[i] - newValue;
                res += diff * diff;
                baseW[i] = newValue;
            }
            res_iters++;

            // calculate global residuum
            laik_switchto_flow(sumD, LAIK_DF_ReduceOut | LAIK_DF_Sum);
            laik_map_def1(sumD, (void**) &sumPtr, 0);
            *sumPtr = res;
            laik_switchto_flow(sumD, LAIK_DF_CopyIn);
            laik_map_def1(sumD, (void**) &sumPtr, 0);
            res = *sumPtr;

            if (iter > 0) {
                t = laik_wtime();
                // current iteration already done
                int diter = (iter + 1) - last_iter;
                double dt = t - t2;
                double gUpdates = 0.000000001 * size; // per iteration
                laik_log(2, "For %d iters: %.3fs, %.3f GF/s, %.3f GB/s",
                         diter, dt,
                         // 2 Flops per update in reg iters, with res 5
                         gUpdates * (5 + 2 * (diter-1)) / dt,
                         // per update 16 bytes read + 8 byte written
                         gUpdates * diter * 24 / dt);
                last_iter = iter + 1;
                t2 = t;
            }

            if (laik_myid(laik_get_dgroup(sumD)) == 0) {
                printf("Residuum after %2d iters: %f\n", iter+1, res);
            }

            if (res < .001) break;
        }
        else {
            for(uint64_t i = from; i < to; i++)
                baseW[i] = 0.5 * (baseR[i-1] + baseR[i+1]);
        }

        // TODO: allow repartitioning
    }

    // for check at end: sum up all just written values
    double sum = 0.0;
    laik_map_def1(dWrite, (void**) &baseW, &countW);
    for(uint64_t i = 0; i < countW; i++) sum += baseW[i];

    // global reduction of local sum values
    laik_switchto_flow(sumD, LAIK_DF_ReduceOut | LAIK_DF_Sum);
    laik_map_def1(sumD, (void**) &sumPtr, 0);
    *sumPtr = sum;
    laik_switchto_flow(sumD, LAIK_DF_CopyIn);
    laik_map_def1(sumD, (void**) &sumPtr, 0);
    sum = *sumPtr;

    // statistics for all iterations and reductions
    // using work load in all tasks
    if (laik_logshown(2)) {
        t = laik_wtime();
        int diter = iter;
        double dt = t - t1;
        double gUpdates = 0.000000001 * size; // per iteration
        laik_log(2, "For %d iters: %.3fs, %.3f GF/s, %.3f GB/s",
                 diter, dt,
                 // 2 Flops per update in reg iters, with res 5
                 gUpdates * (5 * res_iters + 2 * (diter - res_iters)) / dt,
                 // per update 16 bytes read + 8 byte written
                 gUpdates * diter * 24 / dt);
    }

    if (laik_myid(laik_get_dgroup(sumD)) == 0) {
        printf("Global value sum after %d iterations: %f\n",
               iter, sum);
    }

    laik_finalize(inst);
    return 0;
}

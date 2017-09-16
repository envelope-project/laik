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
 * 3d Jacobi example.
 */

#ifdef USE_MPI
#include "laik-backend-mpi.h"
#else
#include "laik-backend-single.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// boundary values
double loRowValue = -5.0, hiRowValue = 10.0;
double loColValue = -10.0, hiColValue = 5.0;
double loPlaneValue = -20.0, hiPlaneValue = 15.0;


int main(int argc, char* argv[])
{
#ifdef USE_MPI
    Laik_Instance* inst = laik_init_mpi(&argc, &argv);
#else
    Laik_Instance* inst = laik_init_single();
#endif
    Laik_Group* world = laik_world(inst);

    int size = 0;
    int maxiter = 0;
    int repart = 0; // enforce repartitioning after <repart> iterations
    bool use_cornerhalo = true; // use halo partitioner including corners?

    int arg = 1;
    while ((argc > arg) && (argv[arg][0] == '-')) {
        if (argv[arg][1] == 'n')
            use_cornerhalo = false;
        if (argv[arg][1] == 'h') {
            printf("Usage: %s [-n] <side width> <maxiter> <repart>\n", argv[0]);
            exit(1);
        }
        arg++;
    }
    if (argc > arg) size = atoi(argv[arg]);
    if (argc > arg + 1) maxiter = atoi(argv[arg + 1]);
    if (argc > arg + 2) repart = atoi(argv[arg + 2]);

    if (size == 0) size = 200; // 8 mio entries
    if (maxiter == 0) maxiter = 50;

    if (laik_myid(world) == 0) {
        printf("%d x %d x %d cells (mem %.1f MB), running %d iterations with %d tasks",
               size, size, size, .000016 * size * size * size,
               maxiter, laik_size(world));
        if (!use_cornerhalo)
            printf(" (halo without corners)");
        printf("\n");
    }

    double *baseR, *baseW, *sumPtr;
    uint64_t zsizeR, zstrideR, ysizeR, ystrideR, xsizeR;
    uint64_t zsizeW, zstrideW, ysizeW, ystrideW, xsizeW;
    uint64_t gx1, gx2, gy1, gy2, gz1, gz2;
    uint64_t x1, x2, y1, y2, z1, z2;

    // two 3d arrays for jacobi, using same space
    Laik_Space* space = laik_new_space_3d(inst, size, size, size);
    Laik_Data* data1 = laik_new_data(world, space, laik_Double);
    Laik_Data* data2 = laik_new_data(world, space, laik_Double);

    // two partitionings:
    // - pWrite: distributes the cells to update
    // - pRead : extends pWrite partitions to allow reading neighbor values
    // partitionings are assigned to either data1/data2, exchanged after
    // every iteration
    Laik_Partitioning *pWrite, *pRead;
    pWrite = laik_new_partitioning(world, space,
                                  laik_new_bisection_partitioner(), 0);
    // this extends pWrite partitions at borders by 1 index on inner borders
    // (the coupling is dynamic: any change in pWrite changes pRead)
    Laik_Partitioner* pr = use_cornerhalo ? laik_new_cornerhalo_partitioner(1) :
                                            laik_new_halo_partitioner(1);
    pRead = laik_new_partitioning(world, space, pr, pWrite);

    // for global sum, used for residuum
    Laik_Data* sumD = laik_new_data_1d(world, laik_Double, 1);
    laik_data_set_name(sumD, "sum");
    laik_switchto_new(sumD, laik_All, LAIK_DF_None);

    // start with writing (= initialization) data1
    Laik_Data* dWrite = data1;
    Laik_Data* dRead = data2;

    // distributed initialization
    laik_switchto(dWrite, pWrite, LAIK_DF_CopyOut);
    laik_my_slice_3d(pWrite, 0, &gx1, &gx2, &gy1, &gy2, &gz1, &gz2);
    // default mapping order for 3d:
    //   with z in [0;zsize[, y in [0;ysize[, x in [0;xsize[
    //   base[z][y][x] is at (base + z * zstride + y * ystride + x)
    laik_map_def1_3d(dWrite, (void**) &baseW,
                     &zsizeW, &zstrideW, &ysizeW, &ystrideW, &xsizeW);
    // arbitrary non-zero values based on global indexes to detect bugs
    for(uint64_t z = 0; z < zsizeW; z++)
        for(uint64_t y = 0; y < ysizeW; y++)
            for(uint64_t x = 0; x < xsizeW; x++)
                baseW[z * zstrideW + y * ystrideW + x] =
                        (double) ((gx1 + x + gy1 + y + gz1 + z) & 6);
    // set fixed boundary values at the 6 faces
    if (gz1 == 0) {
        // front plane
        for(uint64_t y = 0; y < ysizeW; y++)
            for(uint64_t x = 0; x < xsizeW; x++)
                baseW[y * ystrideW + x] = loPlaneValue;
    }
    if (gz2 == size) {
        // back plane
        for(uint64_t y = 0; y < ysizeW; y++)
            for(uint64_t x = 0; x < xsizeW; x++)
                baseW[(zsizeW - 1) * zstrideW + y * ystrideW + x] = hiPlaneValue;
    }
    if (gy1 == 0) {
        // top plane (overwrites global front/back top edge)
        for(uint64_t z = 0; z < zsizeW; z++)
            for(uint64_t x = 0; x < xsizeW; x++)
                baseW[z * zstrideW + x] = loRowValue;
    }
    if (gy2 == size) {
        // bottom plane (overwrites global front/back bottom edge)
        for(uint64_t z = 0; z < zsizeW; z++)
            for(uint64_t x = 0; x < xsizeW; x++)
                baseW[z * zstrideW + (ysizeW - 1) * ystrideW + x] = hiRowValue;
    }
    if (gx1 == 0) {
        // left column, overwrites global left edges
        for(uint64_t z = 0; z < zsizeW; z++)
            for(uint64_t y = 0; y < ysizeW; y++)
                baseW[z * zstrideW + y * ystrideW] = loColValue;
    }
    if (gx2 == size) {
        // right column, overwrites global right edges
        for(uint64_t z = 0; z < zsizeW; z++)
            for(uint64_t y = 0; y < ysizeW; y++)
                baseW[z * zstrideW + y * ystrideW + (xsizeW - 1)] = hiColValue;
    }
    laik_log(2, "Init done\n");

    // for statistics (with LAIK_LOG=2)
    double t, t1 = laik_wtime(), t2 = t1;
    int last_iter = 0;
    int res_iters = 0; // iterations done with residuum calculation

    int iter = 0;
    for(; iter < maxiter; iter++) {
        laik_set_iteration(inst, iter + 1);

        // switch roles: data written before now is read
        if (dRead == data1) { dRead = data2; dWrite = data1; }
        else                { dRead = data1; dWrite = data2; }

        laik_switchto(dRead,  pRead,  LAIK_DF_CopyIn);
        laik_switchto(dWrite, pWrite, LAIK_DF_CopyOut);
        laik_map_def1_3d(dRead,  (void**) &baseR,
                         &zsizeR, &zstrideR, &ysizeR, &ystrideR, &xsizeR);
        laik_map_def1_3d(dWrite, (void**) &baseW,
                         &zsizeW, &zstrideW, &ysizeW, &ystrideW, &xsizeW);

        // local range for which to do 3d stencil, without global edges
        laik_my_slice_3d(pWrite, 0, &gx1, &gx2, &gy1, &gy2, &gz1, &gz2);
        z1 = 0;
        if (gz1 == 0) {
            // front plane
            for(uint64_t y = 0; y < ysizeW; y++)
                for(uint64_t x = 0; x < xsizeW; x++)
                    baseW[y * ystrideW + x] = loPlaneValue;
            z1 = 1;
        }
        z2 = zsizeW;
        if (gz2 == size) {
            // back plane
            for(uint64_t y = 0; y < ysizeW; y++)
                for(uint64_t x = 0; x < xsizeW; x++)
                    baseW[(zsizeW - 1) * zstrideW + y * ystrideW + x] = hiPlaneValue;
            z2 = zsizeW - 1;
        }
        y1 = 0;
        if (gy1 == 0) {
            // top plane (overwrites global front/back top edge)
            for(uint64_t z = 0; z < zsizeW; z++)
                for(uint64_t x = 0; x < xsizeW; x++)
                    baseW[z * zstrideW + x] = loRowValue;
            y1 = 1;
        }
        y2 = ysizeW;
        if (gy2 == size) {
            // bottom plane (overwrites global front/back bottom edge)
            for(uint64_t z = 0; z < zsizeW; z++)
                for(uint64_t x = 0; x < xsizeW; x++)
                    baseW[z * zstrideW + (ysizeW - 1) * ystrideW + x] = hiRowValue;
            y2 = ysizeW - 1;
        }
        x1 = 0;
        if (gx1 == 0) {
            // left column, overwrites global left edges
            for(uint64_t z = 0; z < zsizeW; z++)
                for(uint64_t y = 0; y < ysizeW; y++)
                    baseW[z * zstrideW + y * ystrideW] = loColValue;
            x1 = 1;
        }
        x2 = xsizeW;
        if (gx2 == size) {
            // right column, overwrites global right edges
            for(uint64_t z = 0; z < zsizeW; z++)
                for(uint64_t y = 0; y < ysizeW; y++)
                    baseW[z * zstrideW + y * ystrideW + (xsizeW - 1)] = hiColValue;
            x2 = xsizeW - 1;
        }

        // relocate baseR to be able to use same indexing as with baseW
        if (gx1 > 0) {
            // ghost cells from left neighbor at x=0, move that to -1
            baseR++;
        }
        if (gy1 > 0) {
            // ghost cells from top neighbor at y=0, move that to -1
            baseR += ystrideR;
        }
        if (gz1 > 0) {
            // ghost cells from back neighbor at z=0, move that to -1
            baseR += zstrideR;
        }

        // do jacobi

        // check for residuum every 10 iterations (3 Flops more per update)
        if ((iter % 10) == 0) {

            double vNew, vSum, diff, res, coeff;
            res = 0.0;
            coeff = 1.0 / 6.0;
            for(uint64_t z = z1; z < z2; z++) {
                for(uint64_t y = y1; y < y2; y++) {
                    for(uint64_t x = x1; x < x2; x++) {
                        vSum = baseR[ (z-1) * zstrideR + y * ystrideR + x ] +
                               baseR[ (z+1) * zstrideR + y * ystrideR + x ] +
                               baseR[ z * zstrideR + (y-1) * ystrideR + x ] +
                               baseR[ z * zstrideR + (y+1) * ystrideR + x ] +
                               baseR[ z * zstrideR + y * ystrideR + (x-1) ] +
                               baseR[ z * zstrideR + y * ystrideR + (x+1) ];
                        vNew = coeff * vSum;
                        diff = baseR[ z * zstrideR + y * ystrideR + x] - vNew;
                        res += diff * diff;
                        baseW[z * zstrideW + y * ystrideW + x] = vNew;
                    }
                }
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
                double gUpdates = 0.000000001 * size * size * size; // per iteration
                laik_log(2, "For %d iters: %.3fs, %.3f GF/s, %.3f GB/s",
                         diter, dt,
                         // 6 Flops per update in reg iters, with res 9 (once)
                         gUpdates * (9 + 6 * (diter-1)) / dt,
                         // per update 48 bytes read + 8 byte written
                         gUpdates * diter * 56 / dt);
                last_iter = iter + 1;
                t2 = t;
            }

            if (laik_myid(laik_get_dgroup(sumD)) == 0) {
                printf("Residuum after %2d iters: %f\n", iter+1, res);
            }

            if (res < .001) break;
        }
        else {
            double vNew, vSum, coeff;
            coeff = 1.0 / 6.0;
            for(uint64_t z = z1; z < z2; z++) {
                for(uint64_t y = y1; y < y2; y++) {
                    for(uint64_t x = x1; x < x2; x++) {
                        vSum = baseR[ (z-1) * zstrideR + y * ystrideR + x ] +
                               baseR[ (z+1) * zstrideR + y * ystrideR + x ] +
                               baseR[ z * zstrideR + (y-1) * ystrideR + x ] +
                               baseR[ z * zstrideR + (y+1) * ystrideR + x ] +
                               baseR[ z * zstrideR + y * ystrideR + (x-1) ] +
                               baseR[ z * zstrideR + y * ystrideR + (x+1) ];
                        vNew = coeff * vSum;
                        baseW[z * zstrideW + y * ystrideW + x] = vNew;
                    }
                }
            }
        }

        // TODO: allow repartitioning
    }

    // statistics for all iterations and reductions
    // using work load in all tasks
    if (laik_logshown(2)) {
        t = laik_wtime();
        int diter = iter;
        double dt = t - t1;
        double gUpdates = 0.000000001 * size * size * size; // per iteration
        laik_log(2, "For %d iters: %.3fs, %.3f GF/s, %.3f GB/s",
                 diter, dt,
                 // 6 Flops per update in reg iters, with res 4
                 gUpdates * (9 * res_iters + 6 * (diter - res_iters)) / dt,
                 // per update 48 bytes read + 8 byte written
                 gUpdates * diter * 56 / dt);
    }

    // for check at end: sum up all just written values
    laik_switchto_new(dWrite,  laik_Master,  LAIK_DF_CopyIn);

    if (laik_myid(laik_get_dgroup(dWrite)) == 0) {
        double sum = 0.0;
        laik_map_def1_3d(dWrite, (void**) &baseW,
                         &zsizeW, &zstrideW, &ysizeW, &ystrideW, &xsizeW);
        for(uint64_t z = 0; z < zsizeW; z++)
            for(uint64_t y = 0; y < ysizeW; y++)
                for(uint64_t x = 0; x < xsizeW; x++)
                    sum += baseW[z * zstrideW + y * ystrideW + x];
        printf("Global value sum after %d iterations: %f\n",
               iter, sum);
    }

    laik_finalize(inst);
    return 0;
}

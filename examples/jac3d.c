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

#include <laik.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// boundary values
double loRowValue = -5.0, hiRowValue = 10.0;
double loColValue = -10.0, hiColValue = 5.0;
double loPlaneValue = -20.0, hiPlaneValue = 15.0;


void setBoundary(int size, Laik_AccessPhase *pWrite, Laik_Data* dWrite)
{
    double *baseW;
    uint64_t zsizeW, zstrideW, ysizeW, ystrideW, xsizeW;
    int64_t gx1, gx2, gy1, gy2, gz1, gz2;

    laik_phase_myslice_3d(pWrite, 0, &gx1, &gx2, &gy1, &gy2, &gz1, &gz2);
    // default mapping order for 3d:
    //   with z in [0;zsize[, y in [0;ysize[, x in [0;xsize[
    //   base[z][y][x] is at (base + z * zstride + y * ystride + x)
    laik_map_def1_3d(dWrite, (void**) &baseW,
                     &zsizeW, &zstrideW, &ysizeW, &ystrideW, &xsizeW);

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
}


int main(int argc, char* argv[])
{
    Laik_Instance* inst = laik_init (&argc, &argv);
    Laik_Group* world = laik_world(inst);

    int size = 0;
    int maxiter = 0;
    bool use_cornerhalo = true; // use halo partitioner including corners?
    bool do_profiling = false;
    bool do_sum = false;
    bool do_reservation = false;

    int arg = 1;
    while ((argc > arg) && (argv[arg][0] == '-')) {
        if (argv[arg][1] == 'n') use_cornerhalo = false;
        if (argv[arg][1] == 'p') do_profiling = true;
        if (argv[arg][1] == 's') do_sum = true;
        if (argv[arg][1] == 'r') do_reservation = true;
        if (argv[arg][1] == 'h') {
            printf("Usage: %s [options] <side width> <maxiter> <repart>\n\n"
                   "Options:\n"
                   " -n : use partitioner which does not include corners\n"
                   " -p : write profiling data to 'jac3d_profiling.txt'\n"
                   " -s : print value sum at end (warning: sum done at master)\n"
                   " -r : do space reservation before iteration loop\n"
                   " -h : print this help text and exit\n",
                   argv[0]);
            exit(1);
        }
        arg++;
    }
    if (argc > arg) size = atoi(argv[arg]);
    if (argc > arg + 1) maxiter = atoi(argv[arg + 1]);

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

    // start profiling interface
    if (do_profiling)
        laik_enable_profiling_file(inst, "jac3d_profiling.txt");

    double *baseR, *baseW, *sumPtr;
    uint64_t zsizeR, zstrideR, ysizeR, ystrideR, xsizeR;
    uint64_t zsizeW, zstrideW, ysizeW, ystrideW, xsizeW;
    int64_t gx1, gx2, gy1, gy2, gz1, gz2;
    int64_t x1, x2, y1, y2, z1, z2;

    // two 3d arrays for jacobi, using same space
    Laik_Space* space = laik_new_space_3d(inst, size, size, size);
    Laik_Data* data1 = laik_new_data(space, laik_Double);
    Laik_Data* data2 = laik_new_data(space, laik_Double);

    // two types of access phases into data1 and data2:
    // - pWrite: distributes the cells to update
    // - pRead : extends pWrite partitions to allow reading neighbor values
    // data1/2 are alternativly accessed using pRead/pWrite, exchanged after
    // every iteration
    Laik_AccessPhase *pWrite, *pRead;
    pWrite = laik_new_accessphase(world, space,
                                  laik_new_bisection_partitioner(), 0);
    // this extends pWrite partitions at borders by 1 index on inner borders
    // (the coupling is dynamic: any change in pWrite changes pRead)
    Laik_Partitioner* pr = use_cornerhalo ? laik_new_cornerhalo_partitioner(1) :
                                            laik_new_halo_partitioner(1);
    pRead = laik_new_accessphase(world, space, pr, pWrite);

    if (do_reservation) {
        // reserve and pre-allocate memory for data1/2
        // this is purely optional, and the application still works when we
        // switch to a partitioning not reserved and allocatedd for.
        // However, this makes sure that no allocation happens in the main
        // iteration, and reservation/allocation should be done again on
        // re-partitioning.
        //
        // notes:
        // - both data will be switched to pWrite and pRead
        // - now run partitioners to get actual partitioning for reservation
        // - order is important, as calculating baRead needs baWrite
        Laik_Partitioning* paWrite = laik_phase_run_partitioner(pWrite);
        Laik_Partitioning* paRead  = laik_phase_get_partitioning(pRead);

        Laik_Reservation* r1 = laik_reservation_new(data1);
        laik_reservation_add(r1, paRead);
        laik_reservation_add(r1, paWrite);
        laik_reservation_alloc(r1);

        Laik_Reservation* r2 = laik_reservation_new(data2);
        laik_reservation_add(r2, paRead);
        laik_reservation_add(r2, paWrite);
        laik_reservation_alloc(r2);
    }

    // for global sum, used for residuum
    Laik_Data* sumD = laik_new_data_1d(inst, laik_Double, 1);
    laik_data_set_name(sumD, "sum");
    laik_switchto_new_phase(sumD, world, laik_All, LAIK_DF_None);

    // start with writing (= initialization) data1
    Laik_Data* dWrite = data1;
    Laik_Data* dRead = data2;

    // distributed initialization
    laik_switchto_phase(dWrite, pWrite, LAIK_DF_CopyOut);
    laik_phase_myslice_3d(pWrite, 0, &gx1, &gx2, &gy1, &gy2, &gz1, &gz2);
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

    setBoundary(size, pWrite, dWrite);
    laik_log(2, "Init done\n");

    // for statistics (with LAIK_LOG=2)
    double t, t1 = laik_wtime(), t2 = t1;
    int last_iter = 0;
    int res_iters = 0; // iterations done with residuum calculation

    int iter = 0;
    for(; iter < maxiter; iter++) {
        laik_reset_profiling(inst);
        laik_set_iteration(inst, iter + 1);
        laik_profile_user_start(inst);
        // switch roles: data written before now is read
        if (dRead == data1) { dRead = data2; dWrite = data1; }
        else                { dRead = data1; dWrite = data2; }

        laik_switchto_phase(dRead,  pRead,  LAIK_DF_CopyIn);
        laik_switchto_phase(dWrite, pWrite, LAIK_DF_CopyOut);
        laik_map_def1_3d(dRead,  (void**) &baseR,
                         &zsizeR, &zstrideR, &ysizeR, &ystrideR, &xsizeR);
        laik_map_def1_3d(dWrite, (void**) &baseW,
                         &zsizeW, &zstrideW, &ysizeW, &ystrideW, &xsizeW);

        setBoundary(size, pWrite, dWrite);

        // determine local range for which to do 3d stencil, without global edges
        laik_phase_myslice_3d(pWrite, 0, &gx1, &gx2, &gy1, &gy2, &gz1, &gz2);
        z1 = (gz1 == 0)    ? 1 : 0;
        y1 = (gy1 == 0)    ? 1 : 0;
        x1 = (gx1 == 0)    ? 1 : 0;
        z2 = (gz2 == size) ? (zsizeW - 1) : zsizeW;
        y2 = (gy2 == size) ? (ysizeW - 1) : ysizeW;
        x2 = (gx2 == size) ? (xsizeW - 1) : xsizeW;

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
            for(int64_t z = z1; z < z2; z++) {
                for(int64_t y = y1; y < y2; y++) {
                    for(int64_t x = x1; x < x2; x++) {
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

            if (laik_myid(laik_data_get_group(sumD)) == 0) {
                printf("Residuum after %2d iters: %f\n", iter+1, res);
            }

            if (res < .001) break;
        }
        else {
            double vNew, vSum, coeff;
            coeff = 1.0 / 6.0;
            for(int64_t z = z1; z < z2; z++) {
                for(int64_t y = y1; y < y2; y++) {
                    for(int64_t x = x1; x < x2; x++) {
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
        laik_profile_user_stop(inst);
        laik_writeout_profile();
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

    if (do_sum) {
        // for check at end: sum up all just written values
        laik_switchto_new_phase(dWrite, laik_data_get_group(dWrite),
                                laik_Master,  LAIK_DF_CopyIn);

        if (laik_myid(laik_data_get_group(dWrite)) == 0) {
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
    }

    laik_finalize(inst);
    return 0;
}

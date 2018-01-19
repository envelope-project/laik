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
 * 2d Jacobi example.
 */

#include <laik.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

// boundary values
double loRowValue = -5.0, hiRowValue = 10.0;
double loColValue = -10.0, hiColValue = 5.0;

// to deliberately change block partitioning (if arg 3 provided)
double getTW(int rank, const void* userData)
{
    int v = * ((int*) userData);
    // switch non-equal weigthing on and off
    return 1.0 + (rank * (v & 1));
}

int main(int argc, char* argv[])
{
    Laik_Instance* inst = laik_init (&argc, &argv);
    Laik_Group* world = laik_world(inst);

    int size = 0;
    int maxiter = 0;
    int repart = 0; // enforce repartitioning after <repart> iterations
    bool use_cornerhalo = true; // use halo partitioner including corners?
    bool do_profiling = false;
    bool do_sum = false;

    int arg = 1;
    while ((argc > arg) && (argv[arg][0] == '-')) {
        if (argv[arg][1] == 'n') use_cornerhalo = false;
        if (argv[arg][1] == 'p') do_profiling = true;
        if (argv[arg][1] == 's') do_sum = true;
        if (argv[arg][1] == 'h') {
            printf("Usage: %s [options] <side width> <maxiter> <repart>\n\n"
                   "Options:\n"
                   " -n : use partitioner which does not include corners\n"
                   " -p : write profiling data to 'jac2d_profiling.txt'\n"
                   " -s : print value sum at end (warning: sum done at master)\n"
                   " -h : print this help text and exit\n",
                   argv[0]);
            exit(1);
        }
        arg++;
    }
    if (argc > arg) size = atoi(argv[arg]);
    if (argc > arg + 1) maxiter = atoi(argv[arg + 1]);
    if (argc > arg + 2) repart = atoi(argv[arg + 2]);

    if (size == 0) size = 2500; // 6.25 mio entries
    if (maxiter == 0) maxiter = 50;

    if (laik_myid(world) == 0) {
        printf("%d x %d cells (mem %.1f MB), running %d iterations with %d tasks",
               size, size, .000016 * size * size, maxiter, laik_size(world));
        if (!use_cornerhalo)
            printf(" (halo without corners)");
        if (repart > 0)
            printf("\n  with repartitioning every %d iterations\n", repart);
        printf("\n");
    }

    // start profiling interface
    if (do_profiling)
        laik_enable_profiling_file(inst, "jac2d_profiling.txt");

    double *baseR, *baseW, *sumPtr;
    uint64_t ysizeR, ystrideR, xsizeR;
    uint64_t ysizeW, ystrideW, xsizeW;
    int64_t gx1, gx2, gy1, gy2, x1, x2, y1, y2;

    // two 2d arrays for jacobi, using same space
    Laik_Space* space = laik_new_space_2d(inst, size, size);
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

    // for global sum, used for residuum and value sum at end
    Laik_Data* sumD = laik_new_data_1d(inst, laik_Double, 1);
    laik_data_set_name(sumD, "sum");
    laik_switchto_new_phase(sumD, world, laik_All, LAIK_DF_None);

    // start with writing (= initialization) data1
    Laik_Data* dWrite = data1;
    Laik_Data* dRead = data2;

    // distributed initialization
    laik_switchto_phase(dWrite, pWrite, LAIK_DF_CopyOut);
    laik_phase_myslice_2d(pWrite, 0, &gx1, &gx2, &gy1, &gy2);
    // default mapping order for 2d:
    //   with y in [0;ysize], x in [0;xsize[
    //   base[y][x] is at (base + y * ystride + x)
    laik_map_def1_2d(dWrite, (void**) &baseW, &ysizeW, &ystrideW, &xsizeW);
    // arbitrary non-zero values based on global indexes to detect bugs
    for(uint64_t y = 0; y < ysizeW; y++)
        for(uint64_t x = 0; x < xsizeW; x++)
            baseW[y * ystrideW + x] = (double) ((gx1 + x + gy1 + y) & 6);
    // set fixed boundary values at the 4 edges
    if (gy1 == 0) {
        // top row
        for(uint64_t x = 0; x < xsizeW; x++)
            baseW[x] = loRowValue;
    }
    if (gy2 == size) {
        // bottom row
        for(uint64_t x = 0; x < xsizeW; x++)
            baseW[(ysizeW - 1) * ystrideW + x] = hiRowValue;
    }
    if (gx1 == 0) {
        // left column, may overwrite global (0,0) and (0,size-1)
        for(uint64_t y = 0; y < ysizeW; y++)
            baseW[y * ystrideW] = loColValue;
    }
    if (gx2 == size) {
        // right column, may overwrite global (size-1,0) and (size-1,size-1)
        for(uint64_t y = 0; y < ysizeW; y++)
            baseW[y * ystrideW + xsizeW - 1] = hiColValue;
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

        laik_switchto_phase(dRead,  pRead,  LAIK_DF_CopyIn);
        laik_switchto_phase(dWrite, pWrite, LAIK_DF_CopyOut);
        laik_map_def1_2d(dRead,  (void**) &baseR, &ysizeR, &ystrideR, &xsizeR);
        laik_map_def1_2d(dWrite, (void**) &baseW, &ysizeW, &ystrideW, &xsizeW);

        // local range for which to do 2d stencil, without global edges
        laik_phase_myslice_2d(pWrite, 0, &gx1, &gx2, &gy1, &gy2);
        y1 = 0;
        if (gy1 == 0) {
            // top row
            for(uint64_t x = 0; x < xsizeW; x++)
                baseW[x] = loRowValue;
            y1 = 1;
        }
        y2 = ysizeW;
        if (gy2 == size) {
            // bottom row
            for(uint64_t x = 0; x < xsizeW; x++)
                baseW[(ysizeW - 1) * ystrideW + x] = hiRowValue;
            y2 = ysizeW - 1;
        }
        x1 = 0;
        if (gx1 == 0) {
            // left column, may overwrite global (0,0) and (0,size-1)
            for(uint64_t y = 0; y < ysizeW; y++)
                baseW[y * ystrideW] = loColValue;
            x1 = 1;
        }
        x2 = xsizeW;
        if (gx2 == size) {
            // right column, may overwrite global (size-1,0) and (size-1,size-1)
            for(uint64_t y = 0; y < ysizeW; y++)
                baseW[y * ystrideW + xsizeW - 1] = hiColValue;
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

        // do jacobi

        // check for residuum every 10 iterations (3 Flops more per update)
        if ((iter % 10) == 0) {

            double newValue, diff, res;
            res = 0.0;
            for(int64_t y = y1; y < y2; y++) {
                for(int64_t x = x1; x < x2; x++) {
                    newValue = 0.25 * ( baseR[ (y-1) * ystrideR + x    ] +
                                        baseR[  y    * ystrideR + x - 1] +
                                        baseR[  y    * ystrideR + x + 1] +
                                        baseR[ (y+1) * ystrideR + x    ] );
                    diff = baseR[y * ystrideR + x] - newValue;
                    res += diff * diff;
                    baseW[y * ystrideW + x] = newValue;
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
                double gUpdates = 0.000000001 * size * size; // per iteration
                laik_log(2, "For %d iters: %.3fs, %.3f GF/s, %.3f GB/s",
                         diter, dt,
                         // 4 Flops per update in reg iters, with res 7 (once)
                         gUpdates * (7 + 4 * (diter-1)) / dt,
                         // per update 32 bytes read + 8 byte written
                         gUpdates * diter * 40 / dt);
                last_iter = iter + 1;
                t2 = t;
            }

            if (laik_myid(laik_data_get_group(sumD)) == 0) {
                printf("Residuum after %2d iters: %f\n", iter+1, res);
            }

            if (res < .001) break;
        }
        else {
            double newValue;
            for(int64_t y = y1; y < y2; y++) {
                for(int64_t x = x1; x < x2; x++) {
                    newValue = 0.25 * ( baseR[ (y-1) * ystrideR + x    ] +
                                        baseR[  y    * ystrideR + x - 1] +
                                        baseR[  y    * ystrideR + x + 1] +
                                        baseR[ (y+1) * ystrideR + x    ] );
                    baseW[y * ystrideW + x] = newValue;
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
        double gUpdates = 0.000000001 * size * size; // per iteration
        laik_log(2, "For %d iters: %.3fs, %.3f GF/s, %.3f GB/s",
                 diter, dt,
                 // 2 Flops per update in reg iters, with res 5
                 gUpdates * (7 * res_iters + 4 * (diter - res_iters)) / dt,
                 // per update 32 bytes read + 8 byte written
                 gUpdates * diter * 40 / dt);
    }

    if (do_sum) {
        // for check at end: sum up all just written values
        laik_switchto_new_phase(dWrite, laik_data_get_group(dWrite),
                                laik_Master, LAIK_DF_CopyIn);

        if (laik_myid(laik_data_get_group(dWrite)) == 0) {
            double sum = 0.0;
            laik_map_def1_2d(dWrite, (void**) &baseW, &ysizeW, &ystrideW, &xsizeW);
            for(uint64_t y = 0; y < ysizeW; y++)
                for(uint64_t x = 0; x < xsizeW; x++)
                    sum += baseW[ y * ystrideW + x];
            printf("Global value sum after %d iterations: %f\n",
                   iter, sum);
        }
    }

    laik_finalize(inst);
    return 0;
}

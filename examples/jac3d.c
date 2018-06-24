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


void setBoundary(int size, Laik_Partitioning *pWrite, Laik_Data* dWrite)
{
    double *baseW;
    uint64_t zsizeW, zstrideW, ysizeW, ystrideW, xsizeW;
    int64_t gx1, gx2, gy1, gy2, gz1, gz2;

    // global index ranges of the slice of this process
    laik_my_slice_3d(pWrite, 0, &gx1, &gx2, &gy1, &gy2, &gz1, &gz2);

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
    bool do_exec = false;
    bool do_actions = false;
    bool do_grid = false;
    int xblocks = 0, yblocks = 0, zblocks = 0; // for grid partitioner

    int arg = 1;
    while ((argc > arg) && (argv[arg][0] == '-')) {
        if (argv[arg][1] == 'n') use_cornerhalo = false;
        if (argv[arg][1] == 'p') do_profiling = true;
        if (argv[arg][1] == 's') do_sum = true;
        if (argv[arg][1] == 'r') do_reservation = true;
        if (argv[arg][1] == 'e') do_exec = true;
        if (argv[arg][1] == 'a') do_actions = true;
        if (argv[arg][1] == 'g') do_grid = true;
        if (argv[arg][1] == 'h') {
            printf("Usage: %s [options] <side width> <maxiter> <repart>\n\n"
                   "Options:\n"
                   " -n : use partitioner which does not include corners\n"
                   " -g : use regular grid instead of bisection partitioner\n"
                   " -p : write profiling data to 'jac3d_profiling.txt'\n"
                   " -s : print value sum at end (warning: sum done at master)\n"
                   " -r : do space reservation before iteration loop\n"
                   " -e : pre-calculate transitions to exec in iteration loop\n"
                   " -a : pre-calculate action sequence to exec (includes -e)\n"
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

    if (do_grid) {
        // find grid partitioning with less or equal blocks than processes
        int pcount = laik_size(world);
        int mind = 2 * pcount;
        for(int x = 1; x < pcount; x++)
            for(int y = x; y < pcount; y++) {
                int z = ((double) pcount) / x / y;
                if ((z == 0) || (x * y * z > pcount)) continue;
                int d = abs(y-x) + abs(z-x) + abs(z-y);
                if (mind <= d) continue;
                mind = d;
                zblocks = z; yblocks = y; xblocks = x;
            }
    }

    if (laik_myid(world) == 0) {
        printf("%d x %d x %d cells (mem %.1f MB), running %d iterations with %d tasks",
               size, size, size, .000016 * size * size * size,
               maxiter, laik_size(world));
        if (do_grid)
            printf(" (grid %d x %d x %d)", zblocks, yblocks, xblocks);
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

    // we use two types of partitioners algorithms:
    // - prWrite: cells to update (disjunctive partitioning)
    // - prRead : extends partitionings by haloes, to read neighbor values
    Laik_Partitioner *prWrite, *prRead;
    prWrite = do_grid ? laik_new_grid_partitioner(xblocks, yblocks, zblocks) :
                        laik_new_bisection_partitioner();
    prRead = use_cornerhalo ? laik_new_cornerhalo_partitioner(1) :
                              laik_new_halo_partitioner(1);

    // run partitioners to get partitionings over 3d space and <world> group
    // data1/2 are then alternately accessed using pRead/pWrite
    Laik_Partitioning *pWrite, *pRead;
    pWrite = laik_new_partitioning(prWrite, world, space, 0);
    pRead  = laik_new_partitioning(prRead, world, space, pWrite);
    laik_partitioning_set_name(pWrite, "pWrite");
    laik_partitioning_set_name(pRead, "pRead");

    Laik_Reservation* r1 = 0;
    Laik_Reservation* r2 = 0;
    if (do_reservation) {
        // reserve and pre-allocate memory for data1/2
        // this is purely optional, and the application still works when we
        // switch to a partitioning not reserved and allocated for.
        // However, this makes sure that no allocation happens in the main
        // iteration, and reservation/allocation should be done again on
        // re-partitioning.

        r1 = laik_reservation_new(data1);
        laik_reservation_add(r1, pRead);
        laik_reservation_add(r1, pWrite);
        laik_reservation_alloc(r1);
        laik_data_use_reservation(data1, r1);

        r2 = laik_reservation_new(data2);
        laik_reservation_add(r2, pRead);
        laik_reservation_add(r2, pWrite);
        laik_reservation_alloc(r2);
        laik_data_use_reservation(data2, r2);
    }

    Laik_Transition* toHaloTransition = 0;
    Laik_Transition* toExclTransition = 0;
    Laik_ActionSeq* data1_toHaloActions = 0;
    Laik_ActionSeq* data1_toExclActions = 0;
    Laik_ActionSeq* data2_toHaloActions = 0;
    Laik_ActionSeq* data2_toExclActions = 0;
    if (do_exec || do_actions) {
        toHaloTransition = laik_calc_transition(space,
                                                pWrite, LAIK_DF_CopyOut, LAIK_RO_None,
                                                pRead, LAIK_DF_CopyIn, LAIK_RO_None);
        toExclTransition = laik_calc_transition(space,
                                                pRead, LAIK_DF_CopyIn, LAIK_RO_None,
                                                pWrite, LAIK_DF_CopyOut, LAIK_RO_None);
        if (do_actions) {
            data1_toHaloActions = laik_calc_actions(data1, toHaloTransition, r1, r1);
            data1_toExclActions = laik_calc_actions(data1, toExclTransition, r1, r1);
            data2_toHaloActions = laik_calc_actions(data2, toHaloTransition, r2, r2);
            data2_toExclActions = laik_calc_actions(data2, toExclTransition, r2, r2);
        }
    }

    // for global sum, used for residuum: 1 double accessible by all
    Laik_Space* sp1 = laik_new_space_1d(inst, 1);
    Laik_Partitioning* sumP = laik_new_partitioning(laik_All, world, sp1, 0);
    Laik_Data* sumD = laik_new_data(sp1, laik_Double);
    laik_data_set_name(sumD, "sum");
    laik_switchto_partitioning(sumD, sumP, LAIK_DF_None, LAIK_RO_None);

    // start with writing (= initialization) data1
    Laik_Data* dWrite = data1;
    Laik_Data* dRead = data2;

    // distributed initialization
    laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_CopyOut, LAIK_RO_None);
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

    setBoundary(size, pWrite, dWrite);
    laik_log(2, "Init done\n");

    // set data2 to read to make exec_transtion happy (this is a no-op)
    laik_switchto_partitioning(dRead,  pRead,  LAIK_DF_CopyIn, LAIK_RO_None);

    // for statistics (with LAIK_LOG=2)
    double t, t1 = laik_wtime(), t2 = t1;
    int last_iter = 0;
    int res_iters = 0; // iterations done with residuum calculation

    int iter = 0;
    for(; iter < maxiter; iter++) {
        laik_set_iteration(inst, iter + 1);

        laik_reset_profiling(inst);
        laik_profile_user_start(inst);

        // switch roles: data written before now is read
        if (dRead == data1) { dRead = data2; dWrite = data1; }
        else                { dRead = data1; dWrite = data2; }

        // we show 3 different ways of switching containers among partitionings
        // (1) no preparation: directly switch to another partitioning
        // (2) with pre-calculated transitions between partitiongs: execute it
        // (3) with pre-calculated action sequence for transitions: execute it
        // with (3), it is especially beneficial to use a reservation, as
        // the actions usually directly refer to e.g. MPI calls

        if (do_exec || do_actions) {
            // we did pre-calculation to speed up switches
            if (do_actions) {
                // we pre-calculated the communication action sequences
                if (dRead == data1) {
                    // switch data 1 to halo partitioning
                    laik_exec_actions(data1_toHaloActions);
                    laik_exec_actions(data2_toExclActions);
                }
                else {
                    laik_exec_actions(data2_toHaloActions);
                    laik_exec_actions(data1_toExclActions);
                }
            }
            else {
                // pre-calculation of transitions
                laik_exec_transition(dRead, toHaloTransition);
                laik_exec_transition(dWrite, toExclTransition);
            }
        }
        else {
            // no pre-calculation: switch to partitionings
            laik_switchto_partitioning(dRead,  pRead,  LAIK_DF_CopyIn, LAIK_RO_None);
            laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_CopyOut, LAIK_RO_None);
        }

        laik_map_def1_3d(dRead,  (void**) &baseR,
                         &zsizeR, &zstrideR, &ysizeR, &ystrideR, &xsizeR);
        laik_map_def1_3d(dWrite, (void**) &baseW,
                         &zsizeW, &zstrideW, &ysizeW, &ystrideW, &xsizeW);

        setBoundary(size, pWrite, dWrite);

        // determine local range for which to do 3d stencil, without global edges
        laik_my_slice_3d(pWrite, 0, &gx1, &gx2, &gy1, &gy2, &gz1, &gz2);
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
            laik_switchto_flow(sumD, LAIK_DF_ReduceOut, LAIK_RO_Sum);
            laik_map_def1(sumD, (void**) &sumPtr, 0);
            *sumPtr = res;
            laik_switchto_flow(sumD, LAIK_DF_CopyIn, LAIK_RO_None);
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
    if (laik_log_shown(2)) {
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
        Laik_Group* activeGroup = laik_data_get_group(dWrite);

        // for check at end: sum up all just written values
        Laik_Partitioning* pMaster;
        pMaster = laik_new_partitioning(laik_Master, activeGroup, space, 0);
        laik_switchto_partitioning(dWrite, pMaster, LAIK_DF_CopyIn, LAIK_RO_None);

        if (laik_myid(activeGroup) == 0) {
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

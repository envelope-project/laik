/* This file is part of the LAIK parallel container library.
 * Copyright (c) 2017-2019 Josef Weidendorfer
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
static double loRowValue = -5.0, hiRowValue = 10.0;
static double loColValue = -10.0, hiColValue = 5.0;
static double loPlaneValue = -20.0, hiPlaneValue = 15.0;


void setBoundary(int size, Laik_Partitioning *pWrite, Laik_Data* dWrite)
{
    double *baseW;
    uint64_t zsizeW, zstrideW, ysizeW, ystrideW, xsizeW;
    int64_t gx1, gx2, gy1, gy2, gz1, gz2;

    // global index ranges of the range of this process
    laik_my_range_3d(pWrite, 0, &gx1, &gx2, &gy1, &gy2, &gz1, &gz2);

    // default mapping order for 3d:
    //   with z in [0;zsize[, y in [0;ysize[, x in [0;xsize[
    //   base[z][y][x] is at (base + z * zstride + y * ystride + x)
    laik_get_map_3d(dWrite, 0, (void**) &baseW,
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

//--------------------------------------------------------------
// custom layout factory (used with '-l'): just return lex layout
static Laik_Layout* mylayout_new(int n, Laik_Range* range)
{
    return laik_new_layout_lex(n, range);
}


//--------------------------------------------------------------
// main function
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
    bool use_own_layout = false;
    int xblocks = 0, yblocks = 0, zblocks = 0; // for grid partitioner
    int iter_shrink = 0; // number iterations between shrinks (0: disable)
    int shrink_count = 1;

    int arg = 1;
    while ((argc > arg) && (argv[arg][0] == '-')) {
        if (argv[arg][1] == 'n') use_cornerhalo = false;
        if (argv[arg][1] == 'p') do_profiling = true;
        if (argv[arg][1] == 's') do_sum = true;
        if (argv[arg][1] == 'r') do_reservation = true;
        if (argv[arg][1] == 'e') do_exec = true;
        if (argv[arg][1] == 'a') do_actions = true;
        if (argv[arg][1] == 'g') do_grid = true;
        if (argv[arg][1] == 'l') use_own_layout = true;
        if (argv[arg][1] == 'x' && argc > arg+1) {
            xblocks = atoi(argv[++arg]);
            do_grid = true;
        }
        if (argv[arg][1] == 'i' && argc > arg+1) { iter_shrink = atoi(argv[++arg]); }
        if (argv[arg][1] == 'c' && argc > arg+1) { shrink_count = atoi(argv[++arg]); }
        if (argv[arg][1] == 'h') {
            printf("Usage: %s [options] <side width> <maxiter>\n\n"
                   "Options:\n"
                   " -n        : use partitioner which does not include corners\n"
                   " -g        : use grid partitioning with automatic block size\n"
                   " -x <xgrid>: use grid partitioning with given x block length\n"
                   " -p        : write profiling data to 'jac3d_profiling.txt'\n"
                   " -s        : print value sum at end (warning: sum done at master)\n"
                   " -r        : do space reservation before iteration loop\n"
                   " -e        : pre-calculate transitions to exec in iteration loop\n"
                   " -a        : pre-calculate action sequence to exec (includes -e)\n"
                   " -i <iter> : remove master every <iter> iterations (0: disable)\n"
                   " -c <count>: remove <count> first processes (requires -i)\n"
                   " -l        : test layouts: use own minimal custom layout\n"
                   " -h        : print this help text and exit\n",
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
        int mind = 3 * pcount;
        int xmin = 1, xmax = pcount;
        if ((xblocks > 0) && (xblocks <= pcount)) xmin = xmax = xblocks;
        for(int x = xmin; x <= xmax; x++)
            for(int y = 1; y <= pcount; y++) {
                int z = (int) (((double) pcount) / x / y);
                int pdiff = pcount - x * y * z;
                if ((z == 0) || (pdiff < 0)) continue;
                // minimize idle cores and diff in x/y/z
                int d = abs(y-x) + abs(z-x) + abs(z-y) + 2 * pdiff;
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
        if (iter_shrink > 0)
            printf(" (shrink every %d iterations by %d)", iter_shrink, shrink_count);
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

    // for reservation API test
    double *data1BaseW = 0, *data2BaseW = 0;

    // two 3d arrays for jacobi, using same space
    Laik_Space* space = laik_new_space_3d(inst, size, size, size);
    Laik_Data* data1 = laik_new_data(space, laik_Double);
    Laik_Data* data2 = laik_new_data(space, laik_Double);

    if (use_own_layout) {
        laik_data_set_layout_factory(data1, mylayout_new);
        laik_data_set_layout_factory(data2, mylayout_new);
    }

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
        toHaloTransition = laik_calc_transition(space, pWrite, pRead,
                                                LAIK_DF_Preserve, LAIK_RO_None);
        toExclTransition = laik_calc_transition(space, pRead, pWrite,
                                                LAIK_DF_None, LAIK_RO_None);
        if (do_actions) {
            data1_toHaloActions = laik_calc_actions(data1, toHaloTransition, r1, r1);
            data1_toExclActions = laik_calc_actions(data1, toExclTransition, r1, r1);
            data2_toHaloActions = laik_calc_actions(data2, toHaloTransition, r2, r2);
            data2_toExclActions = laik_calc_actions(data2, toExclTransition, r2, r2);
        }
    }

    // for global sum, used for residuum: 1 double accessible by all
    Laik_Space* sp1 = laik_new_space_1d(inst, 1);
    Laik_Partitioning* pSum = laik_new_partitioning(laik_All, world, sp1, 0);
    Laik_Data* dSum = laik_new_data(sp1, laik_Double);
    laik_data_set_name(dSum, "sum");
    laik_switchto_partitioning(dSum, pSum, LAIK_DF_None, LAIK_RO_None);

    // start with writing (= initialization) data1
    Laik_Data* dWrite = data1;
    Laik_Data* dRead = data2;

    // distributed initialization
    laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_None, LAIK_RO_None);
    laik_my_range_3d(pWrite, 0, &gx1, &gx2, &gy1, &gy2, &gz1, &gz2);

    // default mapping order for 3d:
    //   with z in [0;zsize[, y in [0;ysize[, x in [0;xsize[
    //   base[z][y][x] is at (base + z * zstride + y * ystride + x)
    laik_get_map_3d(dWrite, 0, (void**) &baseW,
                    &zsizeW, &zstrideW, &ysizeW, &ystrideW, &xsizeW);
    // arbitrary non-zero values based on global indexes to detect bugs
    for(uint64_t z = 0; z < zsizeW; z++)
        for(uint64_t y = 0; y < ysizeW; y++)
            for(uint64_t x = 0; x < xsizeW; x++)
                baseW[z * zstrideW + y * ystrideW + x] =
                        (double) ((gx1 + x + gy1 + y + gz1 + z) & 6);

    // for reservation API test
    data1BaseW = baseW;

    setBoundary(size, pWrite, dWrite);
    laik_log(2, "Init done\n");

    // set data2 to pRead to make exec_transition happy (this is a no-op)
    laik_switchto_partitioning(dRead,  pRead, LAIK_DF_None, LAIK_RO_None);

    // for statistics (with LAIK_LOG=2)
    double t, t1 = laik_wtime(), t2 = t1;
    int last_iter = 0;
    int res_iters = 0; // iterations done with residuum calculation

    int next_shrink = iter_shrink;
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

        if (do_actions) {
            // case (3): pre-calculated action sequences
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
        else if (do_exec) {
            // case (2): pre-calculated transitions
            laik_exec_transition(dRead, toHaloTransition);
            laik_exec_transition(dWrite, toExclTransition);
        }
        else {
            // case (1): no pre-calculation: switch to partitionings
            laik_switchto_partitioning(dRead,  pRead,  LAIK_DF_Preserve, LAIK_RO_None);
            laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_None, LAIK_RO_None);
        }

        laik_get_map_3d(dRead,  0, (void**) &baseR,
                        &zsizeR, &zstrideR, &ysizeR, &ystrideR, &xsizeR);
        laik_get_map_3d(dWrite, 0, (void**) &baseW,
                        &zsizeW, &zstrideW, &ysizeW, &ystrideW, &xsizeW);

        setBoundary(size, pWrite, dWrite);

        // determine local range for which to do 3d stencil, without global edges
        laik_my_range_3d(pWrite, 0, &gx1, &gx2, &gy1, &gy2, &gz1, &gz2);
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
        // instead of relocating baseR, we can query address via index g1
        // check this (addr is zero if range is empty - this can happen!)
        Laik_Index g1;
        laik_index_init(&g1, gx1, gy1, gz1);
        double* baseR2 = (double*) laik_get_map_addr(dRead, 0, &g1);
        if (baseR2) assert(baseR == baseR2);

        // for reservation API test: check that write pointer stay the same
        if (do_reservation) {
            if (dWrite == data2) {
                if (data2BaseW == 0) data2BaseW = baseW;
                assert(data2BaseW == baseW);
            } else {
                if (data1BaseW == 0) data1BaseW = baseW;
                assert(data1BaseW == baseW);
            }
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
            laik_switchto_flow(dSum, LAIK_DF_None, LAIK_RO_None);
            laik_get_map_1d(dSum, 0, (void**) &sumPtr, 0);
            *sumPtr = res;
            laik_switchto_flow(dSum, LAIK_DF_Preserve, LAIK_RO_Sum);
            laik_get_map_1d(dSum, 0, (void**) &sumPtr, 0);
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

            if (laik_myid(world) == 0) {
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

        // shrink? TODO: allow repartitioning via external control
        if ((iter_shrink > 0) && (iter == next_shrink) &&
            (laik_size(world) > shrink_count)) {
            static int plist[200];
            assert(shrink_count < 200);
            for(int i = 0; i < shrink_count; i++) plist[i] = i;

            next_shrink += iter_shrink;

            Laik_Group* newWorld = laik_new_shrinked_group(world, shrink_count, plist);
            laik_log(2, "shrinking to size %d (id %d)", laik_size(newWorld), laik_myid(newWorld));

            // run partitioners for shrinked group
            Laik_Partitioning *newpWrite, *newpRead, *newpSum;
            newpWrite = laik_new_partitioning(prWrite, newWorld, space, 0);
            newpRead  = laik_new_partitioning(prRead, newWorld, space, newpWrite);
            newpSum   = laik_new_partitioning(laik_All, newWorld, sp1, 0);
            char name[20];
            sprintf(name, "pWrite-Gr%d", laik_size(newWorld));
            laik_partitioning_set_name(newpWrite, name);
            sprintf(name, "pRead-Gr%d", laik_size(newWorld));
            laik_partitioning_set_name(newpRead, name);

            // reserve memory for new partitionings on shrinked group
            Laik_Reservation* newr1 = 0;
            Laik_Reservation* newr2 = 0;
            if (do_reservation) {
                newr1 = laik_reservation_new(data1);
                laik_reservation_add(newr1, newpRead);
                laik_reservation_add(newr1, newpWrite);
                laik_reservation_alloc(newr1);
                laik_data_use_reservation(data1, newr1);

                newr2 = laik_reservation_new(data2);
                laik_reservation_add(newr2, newpRead);
                laik_reservation_add(newr2, newpWrite);
                laik_reservation_alloc(newr2);
                laik_data_use_reservation(data2, newr2);
            }

            // do pre-calculations for transitions and action sequences on new partitions
            if (do_exec || do_actions) {
                if (do_actions) {
                    // need to free before transitions, as action sequences refer to them
                    laik_aseq_free(data1_toHaloActions);
                    laik_aseq_free(data1_toExclActions);
                    laik_aseq_free(data2_toHaloActions);
                    laik_aseq_free(data2_toExclActions);
                }
                laik_free_transition(toHaloTransition);
                laik_free_transition(toExclTransition);

                toHaloTransition = laik_calc_transition(space, newpWrite, newpRead,
                                                        LAIK_DF_Preserve, LAIK_RO_None);
                toExclTransition = laik_calc_transition(space, newpRead, newpWrite,
                                                        LAIK_DF_None, LAIK_RO_None);
                if (do_actions) {
                    data1_toHaloActions = laik_calc_actions(data1, toHaloTransition, newr1, newr1);
                    data1_toExclActions = laik_calc_actions(data1, toExclTransition, newr1, newr1);
                    data2_toHaloActions = laik_calc_actions(data2, toHaloTransition, newr2, newr2);
                    data2_toExclActions = laik_calc_actions(data2, toExclTransition, newr2, newr2);
                }
            }

            // need to preserve data in dWrite
            laik_switchto_partitioning(dWrite, newpWrite, LAIK_DF_Preserve, LAIK_RO_None);
            laik_switchto_partitioning(dRead,  newpRead,  LAIK_DF_None, LAIK_RO_None);
            laik_switchto_partitioning(dSum,   newpSum,   LAIK_DF_None, LAIK_RO_None);

            if (do_reservation) {
                // free memory of old reservation after switching
                laik_reservation_free(r1);
                laik_reservation_free(r2);
                r1 = newr1;
                r2 = newr2;

                // for reservation API test: update saved pointer
                data1BaseW = data2BaseW = 0;
                if (laik_myid(newWorld) >=0) {
                    laik_get_map_3d(dWrite, 0, (void**) &baseW, 0,0,0,0,0);
                    if (dWrite == data1) data1BaseW = baseW; else data2BaseW = baseW;
                }
            }

            // TODO: release old world and partitiongs
            world  = newWorld;
            pWrite = newpWrite;
            pRead  = newpRead;
            pSum   = newpSum;
        }

        if (laik_myid(world) == -1) break;
    }

    // statistics for all iterations and reductions
    // using work load in all tasks
    if (laik_log_shown(2)) {
        t = laik_wtime();
        int diter = iter;
        double dt = t - t1;
        double gUpdates = 0.000000001 * size * size * size; // per iteration
        laik_log(2, "final for %d iters: %.3fs, %.3f GF/s, %.3f GB/s",
                 diter, dt,
                 // 6 Flops per update in reg iters, with res 4
                 gUpdates * (9 * res_iters + 6 * (diter - res_iters)) / dt,
                 // per update 48 bytes read + 8 byte written
                 gUpdates * diter * 56 / dt);
    }

    if (do_sum) {
        // for check at end: sum up all just written values
        Laik_Partitioning* pMaster;
        pMaster = laik_new_partitioning(laik_Master, world, space, 0);
        laik_switchto_partitioning(dWrite, pMaster, LAIK_DF_Preserve, LAIK_RO_None);

        if (laik_myid(world) == 0) {
            double sum = 0.0;
            laik_get_map_3d(dWrite, 0, (void**) &baseW,
                            &zsizeW, &zstrideW, &ysizeW, &ystrideW, &xsizeW);
            for(uint64_t z = 0; z < zsizeW; z++)
                for(uint64_t y = 0; y < ysizeW; y++)
                    for(uint64_t x = 0; x < xsizeW; x++)
                        sum += baseW[z * zstrideW + y * ystrideW + x];
            printf("Global value sum after %d iterations: %f\n",
                   iter, sum);
        }
    }

    // free memory of reservations
    if (do_reservation) {
        laik_reservation_free(r1);
        laik_reservation_free(r2);
    }

    // free transitions and action sequences
    if (do_exec || do_actions) {
        if (do_actions) {
            // need to free before transitions, as action sequences refer to them
            laik_aseq_free(data1_toHaloActions);
            laik_aseq_free(data1_toExclActions);
            laik_aseq_free(data2_toHaloActions);
            laik_aseq_free(data2_toExclActions);
        }
        laik_free_transition(toHaloTransition);
        laik_free_transition(toExclTransition);
    }

    laik_finalize(inst);
    return 0;
}

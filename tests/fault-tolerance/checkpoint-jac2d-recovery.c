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
#include <laik-internal.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <laik-backend-tcp.h>

// boundary values
double loRowValue = -5.0, hiRowValue = 10.0;
double loColValue = -10.0, hiColValue = 5.0;

bool doRestore = false;
int restoreIteration = -1;
int save_argc; char** save_argv;

int main(int argc, char** argv);

void setBoundary(int size, Laik_Partitioning *pWrite, Laik_Data* dWrite)
{
    double *baseW;
    uint64_t ysizeW, ystrideW, xsizeW;
    int64_t gx1, gx2, gy1, gy2;

    // global index ranges of the slice of this process
    laik_my_slice_2d(pWrite, 0, &gx1, &gx2, &gy1, &gy2);

    // default mapping order for 2d:
    //   with y in [0;ysize[, x in [0;xsize[
    //   base[y][x] is at (base + y * ystride + x)
    laik_map_def1_2d(dWrite, (void**) &baseW, &ysizeW, &ystrideW, &xsizeW);

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
}

// to deliberately change block partitioning (if arg 3 provided)
double getTW(int rank, const void* userData)
{
    int v = * ((int*) userData);
    // switch non-equal weigthing on and off
    return 1.0 + (rank * (v & 1));
}

void errorHandler(void* errors) {
    (void)errors;
    doRestore = true;
    main(save_argc, save_argv);
    exit(0);
}

Laik_Instance* inst;
Laik_Group* world;
Laik_Group* smallWorld;
Laik_Space* space;
Laik_Space* sp1;

// [0]: Global sum, [1]: data1, [2]: data2
Laik_Checkpoint spaceCheckpoints[3];

int main(int argc, char* argv[])
{
    laik_set_loglevel(LAIK_LL_Debug);
    if(inst == NULL) {
        inst = laik_init(&argc, &argv);
        world = laik_world(inst);

        printf("Preparing shrinked world, eliminating rank 1 (world size %i)\n", world->size);
        int elimination[] = {1};
        smallWorld = laik_new_shrinked_group(world, 1, elimination);

        // Set the error handler to be able to recover from
        laik_tcp_set_error_handler(errorHandler);

    } else {
        printf("Instance already allocated, not calling init.\n");
        printf("Activating small world (size %i)\n", smallWorld->size);
        world = smallWorld;

        // Set the error handler to be able to recover from
        laik_tcp_set_error_handler(NULL);

//        printf("Calling LAIK finalize\n");
//        laik_finalize(inst);

//        printf("Creating the new instance\n");
//        laik_init(&argc, &argv);

//        world = laik_world(inst);
    }

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

    printf("Jac_2d parallel with rank %i\n", laik_myid(world));
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
    int64_t gx1, gx2, gy1, gy2;
    int64_t x1, x2, y1, y2;

    // two 2d arrays for jacobi, using same space
    if(space == NULL) {
        space = laik_new_space_2d(inst, size, size);
    }
    Laik_Data* data1 = laik_new_data(space, laik_Double);
    Laik_Data* data2 = laik_new_data(space, laik_Double);

    // we use two types of partitioners algorithms:
    // - prWrite: cells to update (disjunctive partitioning)
    // - prRead : extends partitionings by haloes, to read neighbor values
    Laik_Partitioner *prWrite, *prRead;
    prWrite = laik_new_bisection_partitioner();
    prRead = use_cornerhalo ? laik_new_cornerhalo_partitioner(1) :
                              laik_new_halo_partitioner(1);

    // run partitioners to get partitionings over 2d space and <world> group
    // data1/2 are then alternately accessed using pRead/pWrite
    Laik_Partitioning *pWrite, *pRead;
    pWrite = laik_new_partitioning(prWrite, world, space, 0);
    pRead  = laik_new_partitioning(prRead, world, space, pWrite);
    laik_partitioning_set_name(pWrite, "pWrite");
    laik_partitioning_set_name(pRead, "pRead");

    // for global sum, used for residuum: 1 double accessible by all
    if(sp1 == NULL) {
        sp1 = laik_new_space_1d(inst, 1);
    }
    Laik_Partitioning* sumP = laik_new_partitioning(laik_All, world, sp1, 0);
    Laik_Data* sumD = laik_new_data(sp1, laik_Double);
    laik_data_set_name(sumD, "sum");
    laik_switchto_partitioning(sumD, sumP, LAIK_DF_None, LAIK_RO_None);

    // start with writing (= initialization) data1
    Laik_Data* dWrite = data1;
    Laik_Data* dRead = data2;

    // distributed initialization
    laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_None, LAIK_RO_None);
    laik_my_slice_2d(pWrite, 0, &gx1, &gx2, &gy1, &gy2);

    // default mapping order for 2d:
    //   with y in [0;ysize], x in [0;xsize[
    //   base[y][x] is at (base + y * ystride + x)
    laik_map_def1_2d(dWrite, (void**) &baseW, &ysizeW, &ystrideW, &xsizeW);
    // arbitrary non-zero values based on global indexes to detect bugs
    for(uint64_t y = 0; y < ysizeW; y++)
        for(uint64_t x = 0; x < xsizeW; x++)
            baseW[y * ystrideW + x] = (double) ((gx1 + x + gy1 + y) & 6);

    setBoundary(size, pWrite, dWrite);
    laik_log(2, "Init done\n");

    // for statistics (with LAIK_LOG=2)
    double t, t1 = laik_wtime(), t2 = t1;
    int last_iter = 0;
    int res_iters = 0; // iterations done with residuum calculation

    int iter = 0;

    for(; iter < maxiter; iter++) {
        laik_set_iteration(inst, iter + 1);

        // At every 10 iterations, do a checkpoint
        if(iter % 10 == 9) {
            printf("Creating checkpoint of sum\n");
            spaceCheckpoints[0] = laik_checkpoint_create(inst, sp1, sumD, laik_All, world, LAIK_RO_Max);
            printf("Creating checkpoint of data\n");
            spaceCheckpoints[1] = laik_checkpoint_create(inst, space, dWrite, laik_All, world, LAIK_RO_None);
//            printf("Creating checkpoint 3\n");
//            spaceCheckpoints[2] = laik_checkpoint_create(inst, space, data2, laik_All, world, LAIK_RO_None);
            restoreIteration = iter;
            printf("Checkpoint successful at iteration %i\n", iter);

        } else if(doRestore) {
            // If error happens here, do not try to recover
//            laik_tcp_set_error_handler(NULL);

            printf("Restoring from checkpoint (checkpoint iteration %i)\n", restoreIteration);
            laik_checkpoint_restore(inst, &spaceCheckpoints[0], sp1, sumD);
            laik_checkpoint_restore(inst, &spaceCheckpoints[1], space, data1);
//            laik_checkpoint_restore(inst, &spaceCheckpoints[2], space, data2);
            printf("Restore successful\n");
            iter = restoreIteration;
            doRestore = false;
        }

        // *Randomly* fail on one node
        // Make sure to always use the real world here
        if(laik_myid(laik_world(inst)) == 1 && iter == 35) {
            printf("Oops. Process with rank %i did something silly on iteration %i. Aborting!\n", laik_myid(world), iter);
            abort();
        }

        // switch roles: data written before now is read
        if (dRead == data1) { dRead = data2; dWrite = data1; }
        else                { dRead = data1; dWrite = data2; }

        laik_switchto_partitioning(dRead,  pRead,  LAIK_DF_Preserve, LAIK_RO_None);
        laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_None, LAIK_RO_None);
        laik_map_def1_2d(dRead,  (void**) &baseR, &ysizeR, &ystrideR, &xsizeR);
        laik_map_def1_2d(dWrite, (void**) &baseW, &ysizeW, &ystrideW, &xsizeW);

        setBoundary(size, pWrite, dWrite);

        // local range for which to do 2d stencil, without global edges
        laik_my_slice_2d(pWrite, 0, &gx1, &gx2, &gy1, &gy2);
        y1 = (gy1 == 0)    ? 1 : 0;
        x1 = (gx1 == 0)    ? 1 : 0;
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
            laik_switchto_flow(sumD, LAIK_DF_None, LAIK_RO_None);
            laik_map_def1(sumD, (void**) &sumPtr, 0);
            *sumPtr = res;
            laik_switchto_flow(sumD, LAIK_DF_Preserve, LAIK_RO_Sum);
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
    if (laik_log_shown(2)) {
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
        Laik_Group* activeGroup = laik_data_get_group(dWrite);

        // for check at end: sum up all just written values
        Laik_Partitioning* pMaster;
        pMaster = laik_new_partitioning(laik_Master, activeGroup, space, 0);
        laik_switchto_partitioning(dWrite, pMaster, LAIK_DF_Preserve, LAIK_RO_None);

        if (laik_myid(activeGroup) == 0) {
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

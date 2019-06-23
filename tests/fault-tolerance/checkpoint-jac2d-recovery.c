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
#include "fault_tolerance_test_output.h"
#include "fault_tolerance_test.h"
#include "fault_tolerance_test_hash.h"

// boundary values
double loRowValue = -5.0, hiRowValue = 10.0;
double loColValue = -10.0, hiColValue = 5.0;

int restoreIteration = -1;

int main(int argc, char **argv);

double do_jacobi_iteration(const double *baseR, double *baseW, uint64_t ystrideR, uint64_t ystrideW, int64_t x1,
                           int64_t x2, int64_t y1, int64_t y2);

double calculateGlobalResiduum(double localResiduum, double **sumPtr);

void initialize_write_arbitrary_values(double *baseW, uint64_t ysizeW, uint64_t ystrideW, uint64_t xsizeW, int64_t gx1,
                                       int64_t gy1);

void createCheckpoints(int iter);

void restoreCheckpoints();

void setBoundary(int size, Laik_Partitioning *pWrite, Laik_Data *dWrite) {
    double *baseW;
    uint64_t ysizeW, ystrideW, xsizeW;
    int64_t gx1, gx2, gy1, gy2;

    // global index ranges of the slice of this process
    laik_my_slice_2d(pWrite, 0, &gx1, &gx2, &gy1, &gy2);

    // default mapping order for 2d:
    //   with y in [0;ysize[, x in [0;xsize[
    //   base[y][x] is at (base + y * ystride + x)
    laik_map_def1_2d(dWrite, (void **) &baseW, &ysizeW, &ystrideW, &xsizeW);

    // set fixed boundary values at the 4 edges
    if (gy1 == 0) {
        // top row
        for (uint64_t x = 0; x < xsizeW; x++)
            baseW[x] = loRowValue;
    }
    if (gy2 == size) {
        // bottom row
        for (uint64_t x = 0; x < xsizeW; x++)
            baseW[(ysizeW - 1) * ystrideW + x] = hiRowValue;
    }
    if (gx1 == 0) {
        // left column, may overwrite global (0,0) and (0,size-1)
        for (uint64_t y = 0; y < ysizeW; y++)
            baseW[y * ystrideW] = loColValue;
    }
    if (gx2 == size) {
        // right column, may overwrite global (size-1,0) and (size-1,size-1)
        for (uint64_t y = 0; y < ysizeW; y++)
            baseW[y * ystrideW + xsizeW - 1] = hiColValue;
    }
}

void errorHandler(void *errors) {
    (void) errors;
    printf("Received an error condition, attempting to continue.\n");
}

Laik_Instance *inst;
Laik_Group *world;
Laik_Group *smallWorld;
Laik_Space *space;
Laik_Space *sp1;
Laik_Data *data1;
Laik_Data *data2;
Laik_Data *dSum;
Laik_Partitioner *prWrite, *prRead;


// [0]: Global sum, [1]: data1, [2]: data2
Laik_Checkpoint spaceCheckpoints[3];

int main(int argc, char *argv[]) {
    laik_set_loglevel(LAIK_LL_Info);
//    laik_set_loglevel(LAIK_LL_Debug);
    inst = laik_init(&argc, &argv);
    world = laik_world(inst);

    printf("Preparing shrinked world, eliminating rank 1 (world size %i)\n", world->size);
    int elimination[] = {1};
    smallWorld = laik_new_shrinked_group(world, 1, elimination);

    // Set the error handler to be able to recover from
    laik_tcp_set_error_handler(errorHandler);

    int size = 0;
    int maxiter = 0;
    int repart = 0; // enforce repartitioning after <repart> iterations
    bool use_cornerhalo = true; // use halo partitioner including corners?
    bool do_profiling = false;

    int arg = 1;
    while ((argc > arg) && (argv[arg][0] == '-')) {
        if (argv[arg][1] == 'n') use_cornerhalo = false;
        if (argv[arg][1] == 'p') do_profiling = true;
        if (argv[arg][1] == 'h') {
            printf("Usage: %s [options] <side width> <maxiter> <repart>\n\n"
                   "Options:\n"
                   " -n : use partitioner which does not include corners\n"
                   " -p : write profiling data to 'jac2d_profiling.txt'\n"
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

    TPRINTF("Jac_2d parallel with rank %i\n", laik_myid(world));
    if (laik_myid(world) == 0) {
        TPRINTF("%d x %d cells (mem %.1f MB), running %d iterations with %d tasks",
                size, size, .000016 * size * size, maxiter, laik_size(world));
        if (!use_cornerhalo) TPRINTF(" (halo without corners)");
        if (repart > 0) TPRINTF("\n  with repartitioning every %d iterations\n", repart);
        TPRINTF("\n");
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
    space = laik_new_space_2d(inst, size, size);
    laik_set_space_name(space, "Jacobi Matrix Space");
    data1 = laik_new_data(space, laik_Double);
    laik_data_set_name(data1, "Data 1");
    data2 = laik_new_data(space, laik_Double);
    laik_data_set_name(data2, "Data 2");

    // we use two types of partitioners algorithms:
    // - prWrite: cells to update (disjunctive partitioning)
    // - prRead : extends partitionings by haloes, to read neighbor values
    prWrite = laik_new_bisection_partitioner();
    prRead = use_cornerhalo ? laik_new_cornerhalo_partitioner(1) :
             laik_new_halo_partitioner(1);

    // run partitioners to get partitionings over 2d space and <world> group
    // data1/2 are then alternately accessed using pRead/pWrite
    Laik_Partitioning *pWrite, *pRead;
    pWrite = laik_new_partitioning(prWrite, world, space, 0);
    pRead = laik_new_partitioning(prRead, world, space, pWrite);
    laik_partitioning_set_name(pWrite, "pWrite");
    laik_partitioning_set_name(pRead, "pRead");

    // for global sum, used for residuum: 1 double accessible by all
    sp1 = laik_new_space_1d(inst, 1);
    laik_set_space_name(sp1, "Sum Space");
    dSum = laik_new_data(sp1, laik_Double);
    laik_data_set_name(dSum, "sum");
    Laik_Partitioning *pSum = laik_new_partitioning(laik_All, world, sp1, 0);
    laik_switchto_partitioning(dSum, pSum, LAIK_DF_None, LAIK_RO_None);

    // start with writing (= initialization) data1
    Laik_Data *dWrite = data1;
    Laik_Data *dRead = data2;

    // distributed initialization
    laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_None, LAIK_RO_None);
    laik_my_slice_2d(pWrite, 0, &gx1, &gx2, &gy1, &gy2);

    // default mapping order for 2d:
    //   with y in [0;ysize], x in [0;xsize[
    //   base[y][x] is at (base + y * ystride + x)
    laik_map_def1_2d(dWrite, (void **) &baseW, &ysizeW, &ystrideW, &xsizeW);
    initialize_write_arbitrary_values(baseW, ysizeW, ystrideW, xsizeW, gx1, gy1);

    setBoundary(size, pWrite, dWrite);
    laik_log(2, "Init done\n");

    int iter = 0;

    int nodeStatuses[world->size];

    for (; iter < maxiter; iter++) {
        laik_set_iteration(inst, iter + 1);

        // At every 10 iterations, do a checkpoint
        if (iter == 25) {
//            Laik_Partitioning* pMaster = laik_new_partitioning(laik_Master, world, space, NULL);
//            TPRINTF("Switching READ.\n");
//            laik_switchto_partitioning(dRead, pMaster, LAIK_DF_None, LAIK_RO_None);
//            TPRINTF("Switching WRITE.\n");
//            laik_switchto_partitioning(dWrite, pMaster, LAIK_DF_None, LAIK_RO_None);
//            TPRINTF("Switch OK.\n");

            createCheckpoints(iter);

        }
        if (iter % 10 == 5 && iter == 35) {
            TPRINTF("Attempting to determine global status.\n");
            int numFailed = laik_failure_check_nodes(inst, world, nodeStatuses);
            if (numFailed == 0) {
                TPRINTF("Could not detect a failed node.\n");
            } else {
                // Don't allow any failures while and after recovery
                laik_log(LAIK_LL_Warning, "Deactivating error handler!");
                laik_tcp_set_error_handler(NULL);

                laik_failure_eliminate_nodes(inst, numFailed, nodeStatuses);

                // Re-fetch the world
                world = laik_world_fault_tolerant(inst);

                assert(world->size == 3);
                TPRINTF("Attempting to restore with new world size %i\n", world->size);

                pSum = laik_new_partitioning(laik_All, smallWorld, sp1, 0);
                laik_partitioning_set_name(pSum, "pSum_new");
                pWrite = laik_new_partitioning(prWrite, smallWorld, space, 0);
                laik_partitioning_set_name(pWrite, "pWrite_new");
                pRead = laik_new_partitioning(prRead, smallWorld, space, pWrite);
                laik_partitioning_set_name(pRead, "pRead_new");

                TPRINTF("Switching to new partitionings\n");
                laik_switchto_partitioning(dRead, pRead, LAIK_DF_None, LAIK_RO_None);
                laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_None, LAIK_RO_None);
                laik_switchto_partitioning(dSum, pSum, LAIK_DF_None, LAIK_RO_None);

                //TODO delete
                laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_None, LAIK_RO_None);
                laik_switchto_partitioning(dRead, pWrite, LAIK_DF_None, LAIK_RO_None);

                TPRINTF("Removing failed slices from checkpoints\n");
                if (!laik_checkpoint_remove_failed_slices(&spaceCheckpoints[0], &nodeStatuses)
                    || !laik_checkpoint_remove_failed_slices(&spaceCheckpoints[1], &nodeStatuses)
                    || !laik_checkpoint_remove_failed_slices(&spaceCheckpoints[2], &nodeStatuses)) {
                    TPRINTF("A checkpoint no longer covers its entire space, some data was irreversibly lost. Abort.\n");
                    abort();
                }

                restoreCheckpoints();

                iter = restoreIteration;
                laik_tcp_clear_errors();
                world = smallWorld;
                TPRINTF("Restore complete, cleared errors.\n");

//                TPRINTF("Special: Switching to all partitioning.\n");
//                Laik_Partitioning* pMaster = laik_new_partitioning(laik_Master, world, space, NULL);
//
//                laik_switchto_partitioning(data1, pMaster, LAIK_DF_Preserve, LAIK_RO_None);
//                laik_switchto_partitioning(data2, pMaster, LAIK_DF_Preserve, LAIK_RO_None);
//
//                TPRINTF("Special: Switched to all partitioning.\n");

            }
        }

        // *Randomly* fail on one node
        // Make sure to always use the real world here
//        if (laik_myid(laik_world(inst)) == 1 && iter == 35) {
//            printf("Oops. Process with rank %i did something silly on iteration %i. Aborting!\n", laik_myid(world),
//                   iter);
//            abort();
//        }
        if (iter == 34 && laik_myid(laik_world(inst)) == 1) {
//            if (laik_myid(laik_world(inst)) != 1) {
//                errorHandler(NULL);
//            } else {
            TPRINTF("Oops. Process with rank %i did something silly on iteration %i. Aborting!\n", laik_myid(world),
                    iter);
            abort();
//            }
        }

        // switch roles: data written before now is read
        if (dRead == data1) {
            dRead = data2;
            dWrite = data1;
            TPRINTF("Read: data2, Write: data1\n");
        } else {
            dRead = data1;
            dWrite = data2;
            TPRINTF("Read: data1, Write: data2\n");
        }

        laik_switchto_partitioning(dRead, pRead, LAIK_DF_Preserve, LAIK_RO_None);
        laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_None, LAIK_RO_None);
        TPRINTF("Switched partitionings\n");
        laik_map_def1_2d(dRead, (void **) &baseR, &ysizeR, &ystrideR, &xsizeR);
        laik_map_def1_2d(dWrite, (void **) &baseW, &ysizeW, &ystrideW, &xsizeW);

        if (iter == 25 && world->size == 4) {
            writeDataToFile("dRead_pre_", ".pgm", dRead);
        }
        if (iter == 25 && world->size == 3) {
            writeDataToFile("dRead_post", ".pgm", dRead);
        }

        setBoundary(size, pWrite, dWrite);

        // local range for which to do 2d stencil, without global edges
        laik_my_slice_2d(pWrite, 0, &gx1, &gx2, &gy1, &gy2);
        y1 = (gy1 == 0) ? 1 : 0;
        x1 = (gx1 == 0) ? 1 : 0;
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
        double localResiduum = do_jacobi_iteration(baseR, baseW, ystrideR, ystrideW, x1, x2, y1, y2);
        double globalResiduum = calculateGlobalResiduum(localResiduum, &sumPtr);
        TPRINTF("Residuum after %2d iters: %f (local: %f)\n", iter + 1, globalResiduum, localResiduum);
    }

    laik_finalize(inst);
    return 0;
}

void restoreCheckpoints() {
    TPRINTF("Restoring from checkpoint (checkpoint iteration %i)\n", restoreIteration);
    laik_checkpoint_restore(inst, &spaceCheckpoints[0], sp1, dSum);
    laik_checkpoint_restore(inst, &spaceCheckpoints[1], space, data1);
    laik_checkpoint_restore(inst, &spaceCheckpoints[2], space, data2);
    TPRINTF("Restore successful\n");
}

void createCheckpoints(int iter) {
    TPRINTF("Creating checkpoint of sum\n");
    spaceCheckpoints[0] = laik_checkpoint_create(inst, sp1, dSum, laik_Master, smallWorld, LAIK_RO_Max);
    TPRINTF("Creating checkpoint of data\n");
    spaceCheckpoints[1] = laik_checkpoint_create(inst, space, data1, prWrite, smallWorld, LAIK_RO_None);
//    spaceCheckpoints[1] = laik_checkpoint_create(inst, space, data1, NULL, world, LAIK_RO_None);
    TPRINTF("Creating checkpoint 3\n");
//    spaceCheckpoints[2] = laik_checkpoint_create(inst, space, data2, prWrite, smallWorld, LAIK_RO_None);
    spaceCheckpoints[2] = laik_checkpoint_create(inst, space, data2, prWrite, smallWorld, LAIK_RO_None);
    restoreIteration = iter;
    TPRINTF("Checkpoint successful at iteration %i\n", iter);

}

void initialize_write_arbitrary_values(double *baseW, uint64_t ysizeW, uint64_t ystrideW, uint64_t xsizeW, int64_t gx1,
                                       int64_t gy1) {// arbitrary non-zero values based on global indexes to detect bugs
    for (uint64_t y = 0; y < ysizeW; y++)
        for (uint64_t x = 0; x < xsizeW; x++)
            baseW[y * ystrideW + x] = (double) ((gx1 + x + gy1 + y) & 6);
}

double calculateGlobalResiduum(double localResiduum, double **sumPtr) {// calculate global residuum
    laik_switchto_flow(dSum, LAIK_DF_None, LAIK_RO_None);
    laik_map_def1(dSum, (void **) sumPtr, 0);
    *(*sumPtr) = localResiduum;
    laik_switchto_flow(dSum, LAIK_DF_Preserve, LAIK_RO_Sum);
    laik_map_def1(dSum, (void **) sumPtr, 0);
    localResiduum = *(*sumPtr);

    return localResiduum;
}

double do_jacobi_iteration(const double *baseR, double *baseW, uint64_t ystrideR, uint64_t ystrideW, int64_t x1,
                           int64_t x2, int64_t y1, int64_t y2) {
    double newValue, diff, res;
    res = 0.0;
    for (int64_t y = y1; y < y2; y++) {
        for (int64_t x = x1; x < x2; x++) {
            newValue = 0.25 * (baseR[(y - 1) * ystrideR + x] +
                               baseR[y * ystrideR + x - 1] +
                               baseR[y * ystrideR + x + 1] +
                               baseR[(y + 1) * ystrideR + x]);
            diff = baseR[y * ystrideR + x] - newValue;
            res += diff * diff;
            baseW[y * ystrideW + x] = newValue;
        }
    }
    return res;
}

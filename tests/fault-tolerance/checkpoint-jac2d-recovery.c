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
#include <math.h>
#include "fault_tolerance_test_output.h"
#include "fault_tolerance_test.h"
#include "fault_tolerance_test_hash.h"
#include "util/fault-tolerance-options.h"
#include <unistd.h>

// Red is hard to see, so make it the last slice
//unsigned char colors[][3] = {
//        {128, 255, 0},
//        {0,   255, 255},
//        {0,   128, 255},
//        {255, 0,   0},
//};

unsigned char colors[][3] = {
        {255, 255, 255},
        {255, 255, 255},
        {255, 255, 255},
        {255, 255, 255},
        {255, 255, 255},
        {255, 255, 255},
        {255, 255, 255},
        {255, 255, 255},
        {255, 255, 255},
};

// boundary values
double loRowValue = 1.0, hiRowValue = 1.0;
double loColValue = 1.0, hiColValue = 1.0;
double centerValue = 1.0;
double initVal = 0.1;

int restoreIteration = -1;
int dataFileCounter = 0;

int main(int argc, char **argv);

double do_jacobi_iteration(const double *baseR, double *baseW, uint64_t ystrideR, uint64_t ystrideW, int64_t x1,
                           int64_t x2, int64_t y1, int64_t y2);

double calculateGlobalResiduum(double localResiduum, double **sumPtr);

void initialize_write_arbitrary_values(double *baseW, uint64_t ysizeW, uint64_t ystrideW, uint64_t xsizeW, int64_t gx1,
                                       int64_t gy1);

void createCheckpoints(int iter, int redundancyCount, int rotationDistance, bool delayCheckpointRelease);

void restoreCheckpoints();

void exportDataFile(char *label, Laik_Data *data, bool allRanks, int dataFileCounter);

void exportDataFiles();
void exportDataForVisualization();


void setBoundary(int size, int iteration, Laik_Partitioning *pWrite, Laik_Data *dWrite) {
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

    //Center point
//    int64_t lx, ly;
//    if (laik_global2local_2d(dWrite, size / 2, size / 2, &lx, &ly) != NULL) {
//        baseW[ly * ystrideW + lx] = centerValue;
//    }
//    if (laik_global2local_2d(dWrite, (size - 1) / 2, size / 2, &lx, &ly) != NULL) {
//        baseW[ly * ystrideW + lx] = centerValue;
//    }
//    if (laik_global2local_2d(dWrite, size / 2, (size - 1) / 2, &lx, &ly) != NULL) {
//        baseW[ly * ystrideW + lx] = centerValue;
//    }
//    if (laik_global2local_2d(dWrite, (size - 1) / 2, (size - 1) / 2, &lx, &ly) != NULL) {
//        baseW[ly * ystrideW + lx] = centerValue;
//    }

    (void)iteration;
//    // Create a spinning dot
//    double angle = 0.3 * iteration;
//    int64_t xOffset = (int64_t) (cos(angle) * (size / 4.0) + (size / 2.0));
//    int64_t yOffset = (int64_t) (sin(angle) * (size / 4.0) + (size / 2.0));
//    if (laik_global2local_2d(dWrite, xOffset, yOffset, &lx, &ly) != NULL) {
//        baseW[ly * ystrideW + lx] = centerValue;
//    }
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
Laik_Data *dWrite, *dRead;

void errorHandler(void *errors) {
    (void) errors;
    TRACE_EVENT_S("COMM-ERROR", "");
    TPRINTF("Received an error condition, attempting to continue.\n");
}

// Always dWrite
Laik_Checkpoint *spaceCheckpoint = NULL;

int main(int argc, char *argv[]) {
    laik_set_loglevel(LAIK_LL_Warning);
//    laik_set_loglevel(LAIK_LL_Info);
//    laik_set_loglevel(LAIK_LL_Debug);
    inst = laik_init(&argc, &argv);
    world = laik_world(inst);


//    colors[laik_myid(world)][0] = 128;
//    colors[laik_myid(world)][1] = 255;
//    colors[laik_myid(world)][2] = 0;
//    printf("Preparing shrinked world, eliminating rank 1 (world size %i)\n", world->size);
//    int elimination[] = {1};
//    smallWorld = laik_new_shrinked_group(world, 1, elimination);

    int size = 0;
    int maxiter = 0;
    int repart = 0; // enforce repartitioning after <repart> iterations
    bool use_cornerhalo = true; // use halo partitioner including corners?
    bool do_profiling = false;
    FaultToleranceOptions faultToleranceOptions = FaultToleranceOptionsDefault;

    int arg = 1;
    while ((argc > arg) && (argv[arg][0] == '-')) {
        if (strcmp("-n", argv[arg]) == 0) use_cornerhalo = false;
        else if (strcmp("-p", argv[arg]) == 0) do_profiling = true;
        else if (strcmp("-h", argv[arg]) == 0) {
            printf("Usage: %s [options] <side width> <maxiter> <repart>\n\n"
                   "Options:\n"
                   " -n : use partitioner which does not include corners\n"
                   " -p : write profiling data to 'jac2d_profiling.txt'\n"
                   " -h : print this help text and exit\n"
                   FAULT_TOLERANCE_OPTIONS_HELP,
                   argv[0]);
            exit(1);
        }
        else if (parseFaultToleranceOptions(argc, argv, &arg, laik_myid(world), &faultToleranceOptions)) {
            // Successfully parsed argument, do nothing else
        } else {
            printf("Argument %s was not understood.", argv[arg]);
            exit(1);
        }
        arg++;
    }
    if (argc > arg) size = atoi(argv[arg]);
    if (argc > arg + 1) maxiter = atoi(argv[arg + 1]);
    if (argc > arg + 2) repart = atoi(argv[arg + 2]);

    if (size == 0) size = 1024; // entries
    if (maxiter == 0) maxiter = 50;
    if (faultToleranceOptions.failureCheckFrequency == -1) faultToleranceOptions.failureCheckFrequency = faultToleranceOptions.checkpointFrequency;

    // Set the error handler to be able to recover from if failures are being checked
    if(faultToleranceOptions.failureCheckFrequency > -1) {
        laik_error_handler_set(inst, errorHandler);
    }


    TRACE_INIT(laik_myid(world));
    TRACE_EVENT_START("INIT", "");

    TPRINTF("Jac_2d parallel with rank %i\n", laik_myid(world));
    if (laik_myid(world) == 0) {
        TPRINTF("%d x %d cells (mem %.1f MB), running %d iterations with %d tasks",
                size, size, .000016 * size * size, maxiter, laik_size(world));
        printf("%d x %d cells (mem %.1f MB), running %d iterations with %d tasks.\n",
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
    dWrite = data1;
    dRead = data2;

    // distributed initialization
    laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_None, LAIK_RO_None);
    laik_my_slice_2d(pWrite, 0, &gx1, &gx2, &gy1, &gy2);

    // default mapping order for 2d:
    //   with y in [0;ysize], x in [0;xsize[
    //   base[y][x] is at (base + y * ystride + x)
    laik_map_def1_2d(dWrite, (void **) &baseW, &ysizeW, &ystrideW, &xsizeW);
    initialize_write_arbitrary_values(baseW, ysizeW, ystrideW, xsizeW, gx1, gy1);

    int iter = 0;
    setBoundary(size, iter, pWrite, dWrite);
    laik_log(2, "Init done\n");

    int nodeStatuses[world->size];

    TRACE_EVENT_END("INIT", "");

    for (; iter < maxiter; iter++) {
        laik_set_iteration(inst, iter + 1);
        if(iter % 100 == 0) {
            TRACE_EVENT_S("ITER", "");
        }

        if (faultToleranceOptions.failureCheckFrequency > 0 && iter % faultToleranceOptions.failureCheckFrequency == 0) {
            TPRINTF("Attempting to determine global status.\n");
            TRACE_EVENT_START("FAILURE-CHECK", "");
            Laik_Group *checkGroup = world;
            int numFailed = laik_failure_check_nodes(inst, checkGroup, nodeStatuses);
            TRACE_EVENT_END("FAILURE-CHECK", "");
            if (numFailed == 0) {
                TPRINTF("Could not detect a failed node.\n");
            } else {
                TRACE_EVENT_S("FAILURE-DETECT", "");
                // Don't allow any failures while recovery
                laik_log(LAIK_LL_Info, "Deactivating error handler!");
                laik_error_handler_set(inst, NULL);

                laik_failure_eliminate_nodes(inst, numFailed, nodeStatuses);

                // Re-fetch the world
                world = laik_world_fault_tolerant(inst);
//                world = smallWorld;

//                assert(world->size == 3);
                TPRINTF("Attempting to restore with new world size %i\n", world->size);

                TRACE_EVENT_START("RESTORE", "");
                pSum = laik_new_partitioning(laik_All, world, sp1, 0);
                laik_partitioning_set_name(pSum, "pSum_new");
                pWrite = laik_new_partitioning(prWrite, world, space, 0);
                laik_partitioning_set_name(pWrite, "pWrite_new");
                pRead = laik_new_partitioning(prRead, world, space, pWrite);
                laik_partitioning_set_name(pRead, "pRead_new");

                TPRINTF("Switching to new partitionings\n");
                laik_switchto_partitioning(dRead, pRead, LAIK_DF_None, LAIK_RO_None);
                laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_None, LAIK_RO_None);
                laik_switchto_partitioning(dSum, pSum, LAIK_DF_None, LAIK_RO_None);

                if (!faultToleranceOptions.skipCheckpointRecovery) {
                    TPRINTF("Removing failed slices from checkpoints\n");
                    if (!laik_checkpoint_remove_failed_slices(spaceCheckpoint, checkGroup, nodeStatuses)) {
                        TPRINTF("A checkpoint no longer covers its entire space, some data was irreversibly lost. Abort.\n");
                        abort();
                    }

                    restoreCheckpoints();
                    iter = restoreIteration;
                } else {
                    laik_log(LAIK_LL_Info, "Skipping checkpoint restore.");
                }

                TRACE_EVENT_END("RESTORE", "");
                TPRINTF("Restore complete, cleared errors.\n");

//                TPRINTF("Special: Switching to all partitioning.\n");
//                Laik_Partitioning* pMaster = laik_new_partitioning(laik_Master, world, space, NULL);
//
//                laik_switchto_partitioning(data1, pMaster, LAIK_DF_Preserve, LAIK_RO_None);
//                laik_switchto_partitioning(data2, pMaster, LAIK_DF_Preserve, LAIK_RO_None);
//
//                TPRINTF("Special: Switched to all partitioning.\n");

                // Restored normal state, errors are allowed now
                laik_log(LAIK_LL_Info, "Reactivating error handler!");
                laik_error_handler_set(inst, errorHandler);
            }
        }

        // At every checkpointFrequency iterations, do a checkpoint
        if (faultToleranceOptions.checkpointFrequency > 0 && iter % faultToleranceOptions.checkpointFrequency == 0) {
//            Laik_Partitioning* pMaster = laik_new_partitioning(laik_Master, world, space, NULL);
//            TPRINTF("Switching READ.\n");
//            laik_switchto_partitioning(dRead, pMaster, LAIK_DF_None, LAIK_RO_None);
//            TPRINTF("Switching WRITE.\n");
//            laik_switchto_partitioning(dWrite, pMaster, LAIK_DF_None, LAIK_RO_None);
//            TPRINTF("Switch OK.\n");

            TRACE_EVENT_START("CHECKPOINT", "");
            createCheckpoints(iter, faultToleranceOptions.redundancyCount, faultToleranceOptions.rotationDistance, faultToleranceOptions.delayCheckpointRelease);
            TRACE_EVENT_END("CHECKPOINT", "");
        }


        // If we have reached the fail iteration on this process (only set for the requested processes), then abort the
        // program.
        exitIfFailureIteration(iter, &faultToleranceOptions, inst);

        setBoundary(size, iter, pWrite, dWrite);

        //TODO: Comment back out
//        exportDataForVisualization();
//        exportDataFiles();

        // switch roles: data written before now is read
        if (dRead == data1) {
            dRead = data2;
            dWrite = data1;
//            TPRINTF("Read: data2, Write: data1\n");
        } else {
            dRead = data1;
            dWrite = data2;
//            TPRINTF("Read: data1, Write: data2\n");
        }

        laik_switchto_partitioning(dRead, pRead, LAIK_DF_Preserve, LAIK_RO_None);
        laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_None, LAIK_RO_None);
//        TPRINTF("Switched partitionings\n");
        laik_map_def1_2d(dRead, (void **) &baseR, &ysizeR, &ystrideR, &xsizeR);
        laik_map_def1_2d(dWrite, (void **) &baseW, &ysizeW, &ystrideW, &xsizeW);



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
        if(iter % 100 == 0) {
            TPRINTF("Residuum after %2d iters: %f (local: %f)\n", iter + 1, globalResiduum, localResiduum);
            if(laik_myid(world) == 0) {
                printf("Residuum after %2d iters: %f (local: %f)\n", iter + 1, globalResiduum, localResiduum);
            }
        }
    }

    TRACE_EVENT_START("FINALIZE", "");
    laik_finalize(inst);
    TRACE_EVENT_END("FINALIZE", "");
    return 0;
}

void exportDataFile(char *label, Laik_Data *data, bool allRanks, int dataFileCounter) {//        if (iter == 25 && world->size == 4) {
// export the data to an image
//    Laik_Checkpoint *exportCheckpoint = laik_checkpoint_create(inst, space, data, laik_Master, 0, 0, world,
//                                                               LAIK_RO_None);
    Laik_Checkpoint *exportCheckpoint = laik_checkpoint_create(inst, space, data, laik_All, 0, 0, world,
                                                               LAIK_RO_None);
    if (laik_myid(world) == 0 || allRanks) {
        char filenamePrefix[1024];
        snprintf(filenamePrefix, 1024, "output/data_%s_%i_", label, dataFileCounter);
//            writeDataToFile(filenamePrefix, ".pgm", exportCheckpoint->data);
        writeColorDataToFile(filenamePrefix, ".ppm", exportCheckpoint->data, data->activePartitioning, colors, true);
    }
    laik_checkpoint_free(exportCheckpoint);
    //        }

}

void exportDataForVisualization() {
    exportDataFile("live", dWrite, 1, 0);
    sleep(1);
}

void exportDataFiles() {
    exportDataFile("dW", dWrite, 0, dataFileCounter);
//    exportDataFile("d2", data2);
    if (spaceCheckpoint != NULL) {
        exportDataFile("c1", spaceCheckpoint->data, 0, dataFileCounter);
    }
    dataFileCounter++;
}

void restoreCheckpoints() {
    TPRINTF("Restoring from checkpoint (checkpoint iteration %i)\n", restoreIteration);
//    laik_partitioning_migrate(spaceCheckpoint->data->activePartitioning, world);
    laik_checkpoint_restore(inst, spaceCheckpoint, space, dWrite);
    TPRINTF("Restore successful\n");
}

void createCheckpoints(int iter, int redundancyCount, int rotationDistance, bool delayCheckpointRelease) {
    if(spaceCheckpoint != NULL && !delayCheckpointRelease) {
        TPRINTF("Freeing previous checkpoint from iteration %i\n", restoreIteration);
        laik_free(spaceCheckpoint->data);
    }
    TRACE_EVENT_S("CHECKPOINT-PRE-NEW", "");
    TPRINTF("Creating checkpoint of data\n");
    Laik_Checkpoint* newCheckpoint = laik_checkpoint_create(inst, space, dWrite, prWrite, redundancyCount,
            rotationDistance, world,LAIK_RO_None);
    TRACE_EVENT_S("CHECKPOINT-POST-NEW", "");
    TPRINTF("Checkpoint successful at iteration %i\n", iter);

    if(spaceCheckpoint != NULL && delayCheckpointRelease) {
        TPRINTF("Freeing previous checkpoint from iteration %i\n", restoreIteration);
        laik_free(spaceCheckpoint->data);
    }

    spaceCheckpoint = newCheckpoint;
    restoreIteration = iter;
}

void initialize_write_arbitrary_values(double *baseW, uint64_t ysizeW, uint64_t ystrideW, uint64_t xsizeW, int64_t gx1,
                                       int64_t gy1) {// arbitrary non-zero values based on global indexes to detect bugs
    (void) gx1;
    (void) gy1;
    for (uint64_t y = 0; y < ysizeW; y++)
        for (uint64_t x = 0; x < xsizeW; x++)
//            baseW[y * ystrideW + x] = (double) ((gx1 + x + gy1 + y) & 6);
            baseW[y * ystrideW + x] = initVal;
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

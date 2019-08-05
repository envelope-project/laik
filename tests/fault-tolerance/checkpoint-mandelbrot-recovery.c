//
// Created by Vincent Bode on 23/06/2019.
//
#include <laik.h>
#include <laik-internal.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <laik-backend-tcp.h>
#include <complex.h>
#include "fault_tolerance_test_output.h"

#define x_resolution 100
#define y_resolution 100

#define view_x0 0
#define view_y1 1

#define x_stepsize 0.01
#define y_stepsize 0.01

int main(int argc, char *argv[]) {
    laik_set_loglevel(LAIK_LL_Info);

    Laik_Instance *instance = laik_init(&argc, &argv);
    Laik_Group *world = laik_world(instance);
    Laik_Space *space = laik_new_space_2d(instance, x_resolution, y_resolution);
    Laik_Data *dataReal = laik_new_data(space, laik_Double);
    Laik_Data *dataImaginary = laik_new_data(space, laik_Double);

    Laik_Partitioner *partitioner = laik_new_bisection_partitioner();
    Laik_Partitioning *partitioning = laik_new_partitioning(partitioner, world, space, NULL);
    laik_switchto_partitioning(dataReal, partitioning, LAIK_DF_None, LAIK_RO_None);
    laik_switchto_partitioning(dataImaginary, partitioning, LAIK_DF_None, LAIK_RO_None);

    double *baseReal, *baseImaginary;
    uint64_t sizeRealY, sizeImaginaryY, sizeRealX, sizeImaginaryX, strideRealY, strideImaginaryY;

    laik_map_def1_2d(dataReal, (void **) &baseReal, &sizeRealY, &strideRealY, &sizeRealX);
    laik_map_def1_2d(dataImaginary, (void **) &baseImaginary, &sizeImaginaryY, &strideImaginaryY, &sizeImaginaryX);


    double y;
    double x;

    complex double Z;
    complex double C;

    // Init
    for (unsigned int ly = 0; ly < sizeRealY; ly++) {
        for (unsigned int lx = 0; lx < sizeRealX; lx++) {
            baseReal[ly * strideRealY + lx] = 0;
            baseImaginary[ly * strideImaginaryY + lx] = 0;
        }
    }

    double localResiduum = 0;
    for (int iter = 0; iter < 50; ++iter) {
        Laik_Checkpoint* exportCheckpoint = laik_checkpoint_create(instance, space, dataReal, laik_Master, 1,1,
                                                                   world,
                                                                   LAIK_RO_None);
        if (laik_myid(world) == 0) {
            char filenamePrefix[1024];
            snprintf(filenamePrefix, 1024, "data_%i_%i_", 4 - world->size, iter);
            writeDataToFile(filenamePrefix, ".pgm", exportCheckpoint->data);
        }
        laik_free(exportCheckpoint->data);
        free(exportCheckpoint);

        // Do Mandelbrot update
        for (unsigned int ly = 0; ly < sizeRealY; ly++) {
            for (unsigned int lx = 0; lx < sizeRealX; lx++) {
                int64_t positionX, positionY;
                laik_local2global1_2d(dataReal, lx, ly, &positionX, &positionY);

                x = view_x0 + positionX * x_stepsize;
                y = view_y1 - positionY * y_stepsize;

                Z = baseReal[ly * strideRealY + lx] + baseImaginary[ly * strideImaginaryY + lx] * I;
//
//                C = x_stepsize + y_stepsize * I;
//                Z = Z + C;
//
//                (void) (x + y);
                C = x + y * I;
                Z = Z * Z + C;
//
                baseReal[ly * strideRealY + lx] = creal(Z);
                baseImaginary[ly * strideImaginaryY + lx] = cimag(Z);

                localResiduum += cabs(Z);
            }
        }
        printf("Local residuum iteration %i: %f\n", iter, localResiduum);
    }

    laik_finalize(instance);
}

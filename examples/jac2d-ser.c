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
 * Serial, without LAIK, to validate jac2d LAIK version.
 *
 * Compile with
 *   make jac2d-ser OPT=-O3
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

// boundary values
double loRowValue = -5.0, hiRowValue = 10.0;
double loColValue = -10.0, hiColValue = 5.0;

double wtime()
{
    struct timeval tv;
    gettimeofday(&tv, 0);

    return tv.tv_sec+1e-6*tv.tv_usec;
}

int main(int argc, char* argv[])
{
    int size = 0;
    int maxiter = 0;

    // emulate LAIK log behavior
    bool stats = (getenv("LAIK_LOG") == 0) ? false : true;

    if (argc > 1) size = atoi(argv[1]);
    if (argc > 2) maxiter = atoi(argv[2]);

    if (size == 0) size = 2500; // 6.25 mio entries
    if (maxiter == 0) maxiter = 50;

    printf("%d x %d cells (mem %.1f MB), running %d iterations\n",
           size, size, .000016 * size * size, maxiter);

    double *baseR, *baseW;
    // memory layout is always the same for reading and writing
    uint64_t ysizeR = size, ystrideR = size, xsizeR = size;
    uint64_t ysizeW = size, ystrideW = size, xsizeW = size;

    double* data1 = (double*) malloc(size * size * sizeof(double));
    double* data2 = (double*) malloc(size * size * sizeof(double));

    // start with writing (= initialization) data1
    baseW = data1;
    baseR = data2;
    // arbitrary non-zero values based on (global) indexes to detect bugs
    for(uint64_t y = 0; y < ysizeW; y++)
        for(uint64_t x = 0; x < xsizeW; x++)
            baseW[y * ystrideW + x] = (double) ((x + y) & 6);
    // set fixed boundary values at the 4 edges
    // top row
    for(uint64_t x = 0; x < xsizeW; x++)
        baseW[x] = loRowValue;
    // bottom row
    for(uint64_t x = 0; x < xsizeW; x++)
        baseW[(ysizeW - 1) * ystrideW + x] = hiRowValue;
    // left column, overwrites (0,0) and (0,size-1)
    for(uint64_t y = 0; y < ysizeW; y++)
        baseW[y * ystrideW] = loColValue;
    // right column, overwrites (size-1,0) and (size-1,size-1)
    for(uint64_t y = 0; y < ysizeW; y++)
        baseW[y * ystrideW + xsizeW - 1] = hiColValue;
    if (stats)
        printf("Init done\n");

    // for statistics (with LAIK_LOG)
    double t, t1 = wtime(), t2 = t1;
    int last_iter = 0;
    int res_iters = 0; // iterations done with residuum calculation

    int iter = 0;
    for(; iter < maxiter; iter++) {

        // switch roles: data written before now is read
        if (baseR == data1) { baseR = data2; baseW = data1; }
        else                { baseR = data1; baseW = data2; }

        // write boundary values (not needed, just same as in LAIK version)
        // top row
        for(uint64_t x = 0; x < xsizeW; x++)
            baseW[x] = loRowValue;
        // bottom row
        for(uint64_t x = 0; x < xsizeW; x++)
            baseW[(ysizeW - 1) * ystrideW + x] = hiRowValue;
        // left column, overwrites (0,0) and (0,size-1)
        for(uint64_t y = 0; y < ysizeW; y++)
            baseW[y * ystrideW] = loColValue;
        // right column, overwrites (size-1,0) and (size-1,size-1)
        for(uint64_t y = 0; y < ysizeW; y++)
            baseW[y * ystrideW + xsizeW - 1] = hiColValue;

        // do jacobi

        // check for residuum every 10 iterations (3 Flops more per update)
        if ((iter % 10) == 0) {

            double newValue, diff, res;
            res = 0.0;
            for(uint64_t y = 1; y < ysizeW - 1; y++) {
                for(uint64_t x = 1; x < xsizeW - 1; x++) {
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

            if ((iter > 0) && stats) {
                t = wtime();
                // current iteration already done
                int diter = (iter + 1) - last_iter;
                double dt = t - t2;
                double gUpdates = 0.000000001 * size * size; // per iteration
                printf("For %d iters: %.3fs, %.3f GF/s, %.3f GB/s\n",
                       diter, dt,
                       // 4 Flops per update in reg iters, with res 7 (once)
                       gUpdates * (7 + 4 * (diter-1)) / dt,
                       // per update 32 bytes read + 8 byte written
                       gUpdates * diter * 40 / dt);
                last_iter = iter + 1;
                t2 = t;
            }

            printf("Residuum after %2d iters: %f\n", iter+1, res);

            if (res < .001) break;
        }
        else {
            double newValue;
            for(uint64_t y = 1; y < ysizeW - 1; y++) {
                for(uint64_t x = 1; x < xsizeW - 1; x++) {
                    newValue = 0.25 * ( baseR[ (y-1) * ystrideR + x    ] +
                            baseR[  y    * ystrideR + x - 1] +
                            baseR[  y    * ystrideR + x + 1] +
                            baseR[ (y+1) * ystrideR + x    ] );
                    baseW[y * ystrideW + x] = newValue;
                }
            }
        }

    }

    // for check at end: sum up all just written values
    double sum = 0.0;
    for(uint64_t y = 0; y < ysizeW; y++)
        for(uint64_t x = 0; x < xsizeW; x++)
            sum += baseW[ y * ystrideW + x];

    // statistics for all iterations and reductions
    // using work load in all tasks
    if (stats) {
        t = wtime();
        int diter = iter;
        double dt = t - t1;
        double gUpdates = 0.000000001 * size * size; // per iteration
        printf( "For %d iters: %.3fs, %.3f GF/s, %.3f GB/s\n",
                diter, dt,
                // 2 Flops per update in reg iters, with res 5
                gUpdates * (7 * res_iters + 4 * (diter - res_iters)) / dt,
                // per update 32 bytes read + 8 byte written
                gUpdates * diter * 40 / dt);
    }

    printf("Global value sum after %d iterations: %f\n",
           iter, sum);
    return 0;
}

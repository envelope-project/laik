/* This file is part of LAIK.
 * Vector sum example.
 */

#include "laik.h"

#include <stdio.h>

void main(int argc, char* argv[])
{
    laik_init(&laik_backend_single);

    // allocate global 1d double array: 1 mio entries, equal sized stripes
    Laik_Data* double_arr = laik_alloc(laik_world, LAIK_DT_1D_Double, 1000000);
    // parallel initialization: write 1.0 to own partition
    laik_fill_double(double_arr, 1.0);

    // partial vector sum over own partition via direct access
    double mysum = 0.0, *base;
    uint64_t count, i;
    laik_pin(double_arr, 0, (void**) &base, &count);
    for (i = 0; i < count; i++) {
        mysum += base[i];
    }

    // for collecting partial sums at master, use LAIK's automatic
    // data migration when switching between different partitionings;
    Laik_Data* sum_arr = laik_alloc(laik_world, LAIK_DT_1D_Double, laik_size());
    // set own sum_arr value to partial sum
    laik_fill_double(sum_arr, mysum);
    // switch to master-only partitioning: this transfers values to master
    laik_repartition(sum_arr, LAIK_PT_Single);

    if (laik_myid() == 0) {
        double sum = 0.0;
        laik_pin(sum_arr, 0, (void**) &base, &count);
        for (i = 0; i < count; i++) {
            sum += base[i];
        }
        printf("Result: %f\n", sum);
    }
    laik_finish();
}
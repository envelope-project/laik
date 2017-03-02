/* This file is part of LAIK.
 * Vector sum example.
 */

#include "laik.h"

main()
{
    laik_init(laik_tcp_backend); // use TCP backend

    // allocate global 1d double array: 1 mio entries, equal sized stripes
    Laik_Data double_arr = laik_alloc(LAIK_1D_DOUBLE, 1000000, LAIK_STRIPE);
    // parallel initialization: write 1.0 to own partition
    laik_fill_double(double_arr, 1.0);

    // partial vector sum over own partition via direct access
    uint64_t i, count;
    double mysum = 0.0, *base;
    laik_mydata(double_arr, &base, &count);
    for (i = 0; i < count; i++) {
        mysum += base[i];
    }

    // for collecting partial sums at master, use LAIK's automatic
    // data migration when switching between different partitionings;
    Laik_DS sum_arr = laik_alloc(LAIK_1D, laik_size(), sizeof(double), LAIK_STRIPE);
    // set own sum_arr value to partial sum
    laik_fill_double(sum_arr, mysum);
    // switch to master-only partitioning: this transfers values to master
    laik_repartition(sum_arr, LAIK_SINGLEOWNER, 0);

    if (laik_myid() == 0) {
        double sum = 0.0;
        laik_mydata(sum_arr, &base, &count);
        for (i = 0; i < count; i++) {
            sum += base[i];
        }
        print("Result: %f\n", sum);
    }
    laik_finish();
}
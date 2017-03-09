/* This file is part of LAIK.
 * Vector sum example.
 */

#include "laik.h"
#include "laik-backend-single.h"

#include <stdio.h>

int main(int argc, char* argv[])
{
    Laik_Instance* inst = laik_init_single();
    Laik_Group* world = laik_world(inst);

    // allocate global 1d double array: 1 mio entries, equal sized stripes
    Laik_Data* a = laik_alloc(world, LAIK_DT_1D_Double, 1000000);
    // parallel initialization: write 1.0 to own partition
    laik_fill_double(a, 1.0);

    // partial vector sum over own partition via direct access
    double mysum = 0.0, *base;
    uint64_t count, i;
    laik_map(a, 0, (void**) &base, &count);
    for (i = 0; i < count; i++) {
        mysum += base[i];
    }

    // for collecting partial sums at master, use LAIK's automatic
    // aggregation functionality when switching to new partitioning
    Laik_Data* sum = laik_alloc(world, LAIK_DT_1D_Double, 1);
    laik_set_partitioning(sum, LAIK_PT_All, LAIK_AP_Plus);
    // write partial sum
    laik_fill_double(sum, mysum);
    // master-only partitioning: add partial values to be read at master
    laik_set_partitioning(sum, LAIK_PT_Master, LAIK_AP_ReadOnly);

    if (laik_myid(inst) == 0) {
        laik_map(sum, 0, (void**) &base, &count);
        printf("Result: %f\n", base[0]);
    }
    laik_finalize(inst);

    return 0;
}

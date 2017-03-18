/* This file is part of LAIK.
 * Vector sum example.
 */

#include "laik.h"

#ifdef LAIK_USEMPI
#include "laik-backend-mpi.h"
#else
#include "laik-backend-single.h"
#endif

#include <stdio.h>

int main(int argc, char* argv[])
{
#ifdef LAIK_USEMPI
    Laik_Instance* inst = laik_init_mpi(&argc, &argv);
#else
    Laik_Instance* inst = laik_init_single();
#endif
    Laik_Group* world = laik_world(inst);

    double *base;
    uint64_t count;

    // allocate global 1d double array: 1 mio entries
    Laik_Data* a = laik_alloc_1d(world, 8, 1000000);

    // initialize from master (others do nothing, empty partition)
    laik_set_new_partitioning(a, LAIK_PT_Master, LAIK_AP_WriteOnly);
    if (laik_myid(world) == 0) {
        laik_map(a, 0, (void**) &base, &count);
        for(uint64_t i = 0; i < count; i++) base[i] = (double) i;
    }

    // distribute data equally among all, only for reading
    laik_set_new_partitioning(a, LAIK_PT_Stripe, LAIK_AP_ReadOnly);

    // partial vector sum over own partition via direct access
    double mysum = 0.0;
    laik_map(a, 0, (void**) &base, &count);
    for(uint64_t i = 0; i < count; i++) mysum += base[i];
    printf("Id %d: sum %f\n", laik_myid(world), mysum);

    // for collecting partial sums at master, use LAIK's automatic
    // aggregation functionality when switching to new partitioning
    Laik_Data* sum = laik_alloc_1d(world, 8, 1);
    laik_set_new_partitioning(sum, LAIK_PT_All, LAIK_AP_Plus);
    // write partial sum
    laik_fill_double(sum, mysum);
    // master-only partitioning: add partial values to be read at master
    laik_set_new_partitioning(sum, LAIK_PT_Master, LAIK_AP_ReadOnly);

    if (laik_myid(world) == 0) {
        laik_map(sum, 0, (void**) &base, &count);
        printf("Total sum: %f\n", base[0]);
    }
    laik_finalize(inst);

    return 0;
}

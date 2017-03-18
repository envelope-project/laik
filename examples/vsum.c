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
#include <assert.h>

// for element-wise weighted partitioning: same as index
double getEW(Laik_Index* i, void* d) { return (double) i->i[0]; }

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

    // do partial sums using different partitionings
    double mysum[3] = { 0.0, 0.0, 0.0 };

    // allocate global 1d double array: 1 mio entries
    Laik_Data* a = laik_alloc_1d(world, 8, 1000000);

    // initialize at master (others do nothing, empty partition)
    laik_set_new_partitioning(a, LAIK_PT_Master, LAIK_AP_WriteOnly);
    if (laik_myid(world) == 0) {
        laik_map(a, 0, (void**) &base, &count);
        for(uint64_t i = 0; i < count; i++) base[i] = (double) i;
    }
    // partial sum (according to master partitioning)
    laik_map(a, 0, (void**) &base, &count);
    for(uint64_t i = 0; i < count; i++) mysum[0] += base[i];

    // distribute data equally among all
    laik_set_new_partitioning(a, LAIK_PT_Stripe, LAIK_AP_ReadWrite);
    // partial sum using equally-sized stripes
    laik_map(a, 0, (void**) &base, &count);
    for(uint64_t i = 0; i < count; i++) mysum[1] += base[i];

    // distribution using element-wise weights equal to index
    Laik_Partitioning* p;
    p = laik_new_base_partitioning(laik_get_space(a),
                                   LAIK_PT_Stripe, LAIK_AP_ReadWrite);
    laik_set_index_weight(p, getEW, 0);
    laik_set_partitioning(a, p);
    // partial sum using stripes sized by element weights
    laik_map(a, 0, (void**) &base, &count);
    for(uint64_t i = 0; i < count; i++) mysum[2] += base[i];

    printf("Id %d: partitial sums %.0f, %.0f, %.0f\n",
           laik_myid(world), mysum[0], mysum[1], mysum[2]);

    // for collecting partial sums at master, use LAIK's automatic
    // aggregation functionality when switching to new partitioning
    Laik_Data* sum = laik_alloc_1d(world, 8, 3);
    laik_set_new_partitioning(sum, LAIK_PT_All, LAIK_AP_Plus);
    laik_map(sum, 0, (void**) &base, &count);
    assert(count == 3);
    base[0] = mysum[0];
    base[1] = mysum[1];
    base[2] = mysum[2];
    // master-only partitioning: add partial values to be read at master
    laik_set_new_partitioning(sum, LAIK_PT_Master, LAIK_AP_ReadOnly);

    if (laik_myid(world) == 0) {
        laik_map(sum, 0, (void**) &base, &count);
        printf("Total sums: %.0f, %.0f, %.0f\n", base[0], base[1], base[2]);
    }

    laik_finalize(inst);
    return 0;
}

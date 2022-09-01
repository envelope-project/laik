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
 * Vector sum example (2).
 *
 * Same as vsum, but using 2-cyclic block paritioning; that is, every
 * task will get two ranges in the block partitiongs. To this end, we
 * set the cycle count for the block partitioner to 2.
 */

#include <laik.h>

#include <stdio.h>
#include <assert.h>

// for element-wise weighted partitioning: same as index
double getEW(Laik_Index* i, const void* d)
{
    (void) d; /* suppress compiler warning, par mandatory in interface */

    return (double) i->i[0];
}

// for task-wise weighted partitioning: skip task given as user data
double getTW(int r, const void* d) { return ((long int)d == r) ? 0.0 : 1.0; }

int main(int argc, char* argv[])
{
    Laik_Instance* inst = laik_init (&argc, &argv);
    Laik_Group* world = laik_world(inst);

    int size = 0;
    if (argc > 1) size = atoi(argv[1]);
    if (size == 0) size = 1000000;

    laik_set_phase(inst, 0, "init", NULL);

    double *base;
    uint64_t count;

    // do partial sums using different partitionings
    double mysum[4] = { 0.0, 0.0, 0.0, 0.0 };

    // allocate global 1d double array: <size> entries (default: 1 mio)
    Laik_Data* a = laik_new_data_1d(inst, laik_Double, size);

    laik_set_phase(inst, 1, "master-only", NULL);

    // initialize at master (others do nothing, empty partition)
    laik_switchto_new_partitioning(a, world, laik_Master,
                                   LAIK_DF_None, LAIK_RO_None);
    if (laik_myid(world) == 0) {
        // it is ensured this is exactly one range
        laik_get_map_1d(a, 0, (void**) &base, &count);
        for(uint64_t i = 0; i < count; i++) base[i] = (double) i;
    }
    // partial sum (according to master partitioning)
    laik_get_map_1d(a, 0, (void**) &base, &count);
    for(uint64_t i = 0; i < count; i++) mysum[0] += base[i];
    
    laik_set_phase(inst, 2, "block", NULL);

    // distribute data equally among all
    laik_switchto_new_partitioning(a, world,
                                   laik_new_block_partitioner(0, 2, 0, 0, 0),
                                   LAIK_DF_Preserve, LAIK_RO_None);
    // partial sum using equally-sized blocks, outer loop over ranges
    
    for(int sNo = 0;; sNo++) {
        if (laik_get_map_1d(a, sNo, (void**) &base, &count) == 0) break;
        for(uint64_t i = 0; i < count; i++) mysum[1] += base[i];
    }
    laik_set_phase(inst, 3, "element-wise", NULL);

    // distribution using element-wise weights equal to index
    laik_switchto_new_partitioning(a, world,
                                   laik_new_block_partitioner(0, 2, getEW, 0, 0),
                                   LAIK_DF_Preserve, LAIK_RO_None);
    // partial sum using blocks sized by element weights
    for(int sNo = 0;; sNo++) {
        if (laik_get_map_1d(a, sNo, (void**) &base, &count) == 0) break;
        for(uint64_t i = 0; i < count; i++) mysum[2] += base[i];
    }

    laik_set_phase(inst, 3, "task-wise", NULL);

    if (laik_size(world) > 1) {
        // distribution using task-wise weights: without master
        laik_switchto_new_partitioning(a, world,
                                       laik_new_block_partitioner(0, 2, 0, getTW, 0),
                                       LAIK_DF_Preserve, LAIK_RO_None);
        // partial sum using blocks sized by task weights
        for(int sNo = 0;; sNo++) {
            if (laik_get_map_1d(a, sNo, (void**) &base, &count) == 0) break;
            for(uint64_t i = 0; i < count; i++) mysum[3] += base[i];
        }
    }
    else
        mysum[3] = mysum[0];

    printf("Id %d: partitial sums %.0f, %.0f, %.0f, %.0f\n",
           laik_myid(world), mysum[0], mysum[1], mysum[2], mysum[3]);

    laik_set_phase(inst, 5, "verification", NULL);

    // for collecting partial sums at master, use LAIK's automatic
    // aggregation functionality when switching to new partitioning
    Laik_Data* sum = laik_new_data_1d(inst, laik_Double, 4);
    laik_switchto_new_partitioning(sum, world, laik_All,
                                   LAIK_DF_None, LAIK_RO_None);
    laik_get_map_1d(sum, 0, (void**) &base, &count);
    assert(count == 4);
    for(int i = 0; i < 4; i++) base[i] = mysum[i];

    // master-only partitioning: add partial values to be read at master
    laik_switchto_new_partitioning(sum, world, laik_Master,
                                   LAIK_DF_Preserve, LAIK_RO_Sum);
    if (laik_myid(world) == 0) {
        laik_get_map_1d(sum, 0, (void**) &base, &count);
        printf("Total sums: %.0f, %.0f, %.0f, %.0f\n",
               base[0], base[1], base[2], base[3]);
    }

    laik_finalize(inst);
    return 0;
}

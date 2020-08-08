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
 * Vector sum example.
 *
 * This example initializes a 1d array at master, and uses
 * different partitionings over available tasks to do a vector sum.
 * In the end, LAIK is used to reduce the partial sums into totals.
 *
 * The example shows how the LAIK API is used to specify partitionings.
 * It also tests the LAIK (backend) implementation to correctly
 * distribute the data when switching between partitionings.
 */

#include <laik.h>

#include <stdio.h>
#include <assert.h>

// for element-wise weighted partitioning: same as index
double getEW(Laik_Index* i, const void* d)
{
    (void) d; /* suppress compiler warning. d is mandatory in the interface */

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

    // provides meta-information for logging
    laik_set_phase(inst, 0, "init", NULL);

    double *base;
    uint64_t count;

    // do partial sums using different partitionings
    double mysum[4] = { 0.0, 0.0, 0.0, 0.0 };

    Laik_Space* space;
    Laik_Data* array;
    Laik_Partitioner *parter2, *parter3, *parter4;
    Laik_Partitioning *part1, *part2, *part3, *part4;

    // define global 1d double array with <size> entries (default: 1 mio)
    space = laik_new_space_1d(inst, size);
    array = laik_new_data(space, laik_Double);

    // allocate and initialize at master (others have empty partition)
    part1 = laik_new_partitioning(laik_Master, world, space, 0);
    laik_switchto_partitioning(array, part1, LAIK_DF_None, LAIK_RO_None);
    if (laik_myid(world) == 0) {
        // it is ensured this is exactly one slice
        laik_get_map_1d(array, 0, (void**) &base, &count);
        for(uint64_t i = 0; i < count; i++) base[i] = (double) i;
    }

    // partial sum (according to master partitioning)
    laik_get_map_1d(array, 0, (void**) &base, &count);
    for(uint64_t i = 0; i < count; i++) mysum[0] += base[i];

    // distribute data equally among all
    parter2 = laik_new_block_partitioner1();
    part2 = laik_new_partitioning(parter2, world, space, 0);
    laik_switchto_partitioning(array, part2, LAIK_DF_Preserve, LAIK_RO_None);

    // partial sum using equally-sized blocks
    laik_get_map_1d(array, 0, (void**) &base, &count);
    for(uint64_t i = 0; i < count; i++) mysum[1] += base[i];
    
    laik_set_phase(inst, 1, "element-wise", NULL);

    // distribution using element-wise weights equal to index
    parter3 = laik_new_block_partitioner_iw1(getEW, 0);
    part3 = laik_new_partitioning(parter3, world, space, 0);
    laik_switchto_partitioning(array, part3, LAIK_DF_Preserve, LAIK_RO_None);

    // partial sum using blocks sized by element weights
    laik_get_map_1d(array, 0, (void**) &base, &count);
    for(uint64_t i = 0; i < count; i++) mysum[2] += base[i];

    laik_set_phase(inst, 2, "task-wise", NULL);

    if (laik_size(world) > 1) {
        // distribution using task-wise weights: without master
        parter4 = laik_new_block_partitioner_tw1(getTW, 0);
        part4 = laik_new_partitioning(parter4, world, space, 0);
        laik_switchto_partitioning(array, part4, LAIK_DF_Preserve, LAIK_RO_None);

        // partial sum using blocks sized by task weights
        laik_get_map_1d(array, 0, (void**) &base, &count);
        for(uint64_t i = 0; i < count; i++) mysum[3] += base[i];

        int removeList[1] = {0};
        Laik_Group* g2 = laik_new_shrinked_group(world, 1, removeList);
        // we can migrate used partitionings to new groups as long as added or
        // removed processes do not matter (ie. have empty partitions).
        // this works here as process 0 got empty partition above, and is
        // removed in g2
        laik_partitioning_migrate(part4, g2);
        printf("My world ID %d, in shrinked group: %d\n",
               laik_myid(world), laik_myid(g2));

        mysum[3] = 0;
        if (laik_myid(g2) >= 0) {
            // I am part of g2
            laik_get_map_1d(array, 0, (void**) &base, &count);
            for(uint64_t i = 0; i < count; i++) mysum[3] += base[i];
        }
    }
    else
        mysum[3] = mysum[0];

    printf("Id %d: partitial sums %.0f, %.0f, %.0f, %.0f\n",
           laik_myid(world), mysum[0], mysum[1], mysum[2], mysum[3]);

    // for collecting partial sums at master, use LAIK's automatic
    // aggregation functionality when switching to new partitioning
    Laik_Space* sumspace;
    Laik_Data* sumdata;
    Laik_Partitioning* sumpart1, *sumpart2;
    sumspace = laik_new_space_1d(inst, 4);
    sumdata  = laik_new_data(sumspace, laik_Double);
    sumpart1 = laik_new_partitioning(laik_All, world, sumspace, 0);
    laik_set_initial_partitioning(sumdata, sumpart1);
    // directly provide mysum array as input data
    laik_set_map_memory(sumdata, 0, mysum, 4 * sizeof(double));

    laik_set_phase(inst, 3, "master-only", NULL);
 
    // master-only partitioning: add partial values to be read at master
    sumpart2 = laik_new_partitioning(laik_Master, world, sumspace, 0);
    laik_switchto_partitioning(sumdata, sumpart2, LAIK_DF_Preserve, LAIK_RO_Sum);
    if (laik_myid(world) == 0) {
        laik_get_map_1d(sumdata, 0, (void**) &base, &count);
        printf("Total sums: %.0f, %.0f, %.0f, %.0f\n",
               base[0], base[1], base[2], base[3]);
    }

    laik_finalize(inst);
    return 0;
}

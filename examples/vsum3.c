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
 * This example initializes an 1d array at master, then distributes
 * the array equally over all processes, and calcuates the sum a few
 * times, allowing world resizes.
 */

#include <laik.h>

#include <stdio.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    Laik_Instance* inst = laik_init (&argc, &argv);
    Laik_Group* world = laik_world(inst);
    int phase = laik_phase(inst);

    int size = 0, maxiter = 0;
    if (argc > 1) maxiter = atoi(argv[1]);
    if (argc > 2) size = atoi(argv[2]);
    if (maxiter == 0) maxiter = 10;
    if (size == 0) size = 10000;

    // define global 1d double array with <size> entries (default: 1 mio)
    Laik_Space* space = laik_new_space_1d(inst, size);
    Laik_Data*  array = laik_new_data(space, laik_Double);
    Laik_Partitioner* bp = laik_new_block_partitioner1();

    // data object with 1 double to sum up partial values
    double mysum;
    Laik_Space* sumspace = laik_new_space_1d(inst, 1);
    Laik_Data*  sumdata  = laik_new_data(sumspace, laik_Double);
    Laik_Partitioning* sp1 = laik_new_partitioning(laik_All, world, sumspace, 0);
    Laik_Partitioning* sp2 = laik_new_partitioning(laik_Master, world, sumspace, 0);
    laik_set_initial_partitioning(sumdata, sp1);
    laik_set_map_memory(sumdata, 0, &mysum, sizeof(double));

    double *base;
    uint64_t count, from, to;
    Laik_Partitioning *part1, *part2;

    if (phase == 0) {
        // initial process: initialize at master
        part1 = laik_new_partitioning(laik_Master, world, space, 0);
        laik_switchto_partitioning(array, part1, LAIK_DF_None, LAIK_RO_None);
        laik_get_map_1d(array, 0, (void**) &base, &count);
        for(uint64_t i = 0; i < count; i++) base[i] = (double) i;
    }
    else {
        // joining
        Laik_Group* parent = laik_group_parent(world);
        part1 = laik_new_partitioning(bp, parent, space, 0);
    }

    while(1) {
        part2 = laik_new_partitioning(bp, world, space, 0);
        laik_switchto_partitioning(array, part2, LAIK_DF_Preserve, LAIK_RO_None);

        laik_get_map_1d(array, 0, (void**) &base, &count);
        mysum = 0.0;
        for(uint64_t i = 0; i < count; i++) mysum += base[i];
        laik_my_slice_1d(part2, 0, &from, &to);
        printf("Phase %d, Epoch %d, Proc %d/%d: sum of %ld values at %ld - %ld : %.0f\n",
               phase, laik_epoch(inst), laik_myid(world), laik_size(world),
               count, from, to - 1, mysum);

        laik_switchto_partitioning(sumdata, sp2, LAIK_DF_Preserve, LAIK_RO_Sum);
        if (laik_myid(world) == 0)
            printf("Total sum: %.0f\n", mysum);

        if (phase >= maxiter) break;
        sleep(1);
        phase++;

        // allow resize of world and get new world
        Laik_Group* newworld = laik_allow_world_resize(inst, phase);
        if (laik_myid(newworld) < 0) break;

        if (newworld != world) {
            laik_release_group(world);
            world = newworld;
            sp1 = laik_new_partitioning(laik_All, world, sumspace, 0);
            sp2 = laik_new_partitioning(laik_Master, world, sumspace, 0);
        }

        part1 = part2;
        laik_switchto_partitioning(sumdata, sp1, LAIK_DF_None, LAIK_RO_None);
    }

    laik_finalize(inst);
    return 0;
}

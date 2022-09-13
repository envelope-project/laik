/* This file is part of the LAIK parallel container library.
 * Copyright (c) 2022 Amir Raoofy, Josef Weidendorfer
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
 * Ping-pong micro-benchmark
 *
 * Update values of an array repeatedly between
 * pair processes with even and odd ranks (or 1st and 2nd half of ranks)
 */


#include <laik.h>
#include <assert.h>
#include <stdio.h>

// custom partitioner: pairs can access pieces depending on phase 0/1
// e.g. with 4 processes (0-3) and 1d space with 1000 elems
// - phase 0: proc 0 has access to [0-500[, proc 2 to [500-1000[
// - phase 1: proc 1 has access to [0-500[, proc 3 to [500-1000[
void runPairParter(Laik_RangeReceiver* r, Laik_PartitionerParams* p)
{
    int phase = *(int*) laik_partitioner_data(p->partitioner);
    assert((phase == 0) || (phase == 1));
    int pairs = laik_size(p->group) / 2;
    Laik_Space* space = p->space;
    int64_t size = laik_space_size(space);
 
    Laik_Range range;
    for(int p = 0; p < pairs; p++) {
        // array is split up in consecutive pieces among pairs
        laik_range_init_1d(&range, space,
                           size * p / pairs, size * (p+1) / pairs);
        // give it to even or odd process in pair, depending on phase
        laik_append_range(r, 2 * p + phase, &range, 0, 0);
    }
}


int main(int argc, char* argv[])
{
    Laik_Instance* instance = laik_init(&argc, &argv);
    Laik_Group *world = laik_world(instance);

    // default run mode
    int use_reservation = 1;
    int use_actions = 1;
    // run parameters (defaults are set after parsing arguments)
    long size = 0;
    int iters = 0;

    // parse command line arguments
    int arg = 1;
    while((arg < argc) && (argv[arg][0] == '-')) {
        if (argv[arg][1] == 'r') use_reservation = 0;
        if (argv[arg][1] == 'a') use_actions = 0;
        if (argv[arg][1] == 'h') {
            printf("Ping-pong micro-benchmark for LAIK\n"
                   "Usage: %s [options] [<size> [<iters>]]\n"
                   "\nArguments:\n"
                   " <size>  : number of double entries transfered (def: 100M)\n"
                   " <iters> : number of repetitions (def: 10)\n"
                   "\nOptions:\n"
                   " -r: do not use reservation\n"
                   " -a: do not pre-calculate action sequence\n"
                   " -h: this help text\n", argv[0]);
            exit(1);
        }
        arg++;
    }
    if (argc > arg) size = atol(argv[arg]);
    if (argc > arg + 1) iters = atoi(argv[arg + 1]);

    // set to defaults if not set by arguments
    if (size == 0) size = 100000000;
    if (iters == 0) iters = 10;

    // print benchmark run parameters
    int myid = laik_myid(world);
    if (myid == 0){
        printf("Run %d iterations (%ld doubles) [%sreservation/%sactions]\n",
               iters, size,
               use_reservation ? "with ":"no ", use_actions ? "with ":"no ");
    }

    // setup LAIK objects

    int worldsize = laik_size(world);
    if (worldsize < 2) {
        printf("Error: cannot run ping-pong with 1 process\n");
        laik_finalize(instance);
        exit(1);
    }

    Laik_Space* space = laik_new_space_1d(instance, size);
    Laik_Data* array = laik_new_data(space, laik_Double);

    // run the ping-pong between first and last process in world
    Laik_Partitioner *pr0, *pr1;
    Laik_Partitioning *p0, *p1;
    int phase0 = 0, phase1 = 1;
    pr0 = laik_new_partitioner("even", runPairParter, &phase0, 0);
    pr1 = laik_new_partitioner("odd", runPairParter, &phase1, 0);
    p0 = laik_new_partitioning(pr0, world, space, 0);
    p1 = laik_new_partitioning(pr1, world, space, 0);

    Laik_Reservation* reservation = 0;
    if (use_reservation) {
        reservation = laik_reservation_new(array);
        laik_reservation_add(reservation, p0);
        laik_reservation_add(reservation, p1);
        laik_reservation_alloc(reservation);
        laik_data_use_reservation(array, reservation);
    }

    Laik_ActionSeq* actionsToP0 = 0;
    Laik_ActionSeq* actionsToP1 = 0;
    if (use_actions) {
        // pre-calculate transitions and action sequences
        Laik_Transition *p0_to_p1, *p1_to_p0;
        p0_to_p1 = laik_calc_transition(space, p0, p1, LAIK_DF_Preserve, LAIK_RO_Sum);
        p1_to_p0 = laik_calc_transition(space, p1, p0, LAIK_DF_Preserve, LAIK_RO_Sum);
        actionsToP1 = laik_calc_actions(array, p0_to_p1, reservation, reservation);
        actionsToP0 = laik_calc_actions(array, p1_to_p0, reservation, reservation);
    }

    // initialization by even procs
    laik_switchto_partitioning(array, p0, LAIK_DF_None, LAIK_RO_None);
    double * base;
    uint64_t count;
    laik_get_map_1d(array, 0, (void**) &base, &count);
    for(uint64_t i=0; i < count; ++i)
        base[i] = (double) i;

    // ping pong
    double start_time, end_time;
    start_time = laik_wtime();
    for(int it=0; it<iters; ++it) {
        // ping
        if (use_actions)
            laik_exec_actions(actionsToP1);
        else
            laik_switchto_partitioning(array, p1, LAIK_DF_Preserve, LAIK_RO_None);

        // pong
        if (use_actions)
            laik_exec_actions(actionsToP0);
        else
            laik_switchto_partitioning(array, p0, LAIK_DF_Preserve, LAIK_RO_None);
    }
    end_time = laik_wtime();

    if (myid == 0) {
        // statistics
        printf("Time: %lf s (average per iteration: %lf ms)\n",
	       end_time - start_time,
               (end_time - start_time) * 1e3 / (double)iters);
        printf("GB/s: %lf\n",
               8.0 * 2 * iters * size / (end_time - start_time) / 1.0e9 );
    }

    laik_finalize(instance);
    return 0;
}

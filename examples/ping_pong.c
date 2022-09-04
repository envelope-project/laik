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
 * Update values of an array repeatedly between process 0 and 1
 */


#include <laik.h>
#include <stdio.h>

// custom partitioner: single process gets access to all values
void runAllSingle(Laik_RangeReceiver* r, Laik_PartitionerParams* p)
{
    int proc = *(int*) laik_partitioner_data(p->partitioner);
    Laik_Space* space = p->space;
 
    Laik_Range range;
    laik_range_init_1d(&range, space, 0, laik_space_size(space));
    laik_append_range(r, proc, &range, 0, 0);
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
    if (argc > arg) size = atoi(argv[arg]);
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
    int proc0 = 0;
    int proc1 = worldsize - 1;
    pr0 = laik_new_partitioner("allFirst", runAllSingle, &proc0, 0);
    pr1 = laik_new_partitioner("allLast", runAllSingle, &proc1, 0);
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

    // initialization by proc 0
    laik_switchto_partitioning(array, p0, LAIK_DF_None, LAIK_RO_None);
    double * base;
    laik_get_map_1d(array, 0, (void**) &base, 0);
    if (myid == 0){
        for (long i=0; i<size; ++i)
            base[i] = (double) i;
    }

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

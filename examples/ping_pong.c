#include "laik.h"
#include <stdio.h>

#define  MAX_LEN 1 << 18        /* maximum vector length        */
#define  TRIALS  100            /* trials for each msg length   */
#define  PROC_0  0              /* processor 0                  */
#define  B0_TYPE 176            /* message "types"              */
#define  B1_TYPE 177



void runPart(Laik_RangeReceiver* r, Laik_PartitionerParams* p)
{
    
    int task = (int) laik_partitioner_data(p->partitioner);
    Laik_Space* space = p->space;
    Laik_Range range;
 
    laik_range_init_1d(&range, space, 0, laik_space_size(space));
    laik_append_range(r, task, &range, 0, 0);
}

Laik_Partitioner* get_partitioner(int task)
{
    return laik_new_partitioner("partitioner", runPart,
                                (void*) task, 0);
}


int main(int argc, char* argv[])
{
    Laik_Instance* instance = laik_init(&argc, &argv);
    Laik_Group *world = laik_world(instance);
    int worldsize = laik_size(world);
    int myid = laik_myid(world);

    double start_time, end_time;        /* "wallclock" times            */

    long size = 0;
    if (argc > 1) size = atoi(argv[1]);
    if (size == 0) size = 1L << 30;

    int trials = 0;
    if (argc > 2) trials = atoi(argv[2]);
    if (trials == 0) trials = 10;

    Laik_Space* space;
    Laik_Data* array;
    space = laik_new_space_1d(instance, size);
    array = laik_new_data(space, laik_Double);

    Laik_Partitioning *p0, *p1;

    p0 = laik_new_partitioning(get_partitioner(0), world, space, 0);
    p1 = laik_new_partitioning(get_partitioner(1), world, space, 0);

    Laik_Reservation* reservation = 0;
    reservation = laik_reservation_new(array);
    laik_reservation_add(reservation, p0);
    laik_reservation_add(reservation, p1);
    laik_reservation_alloc(reservation);
    laik_data_use_reservation(array, reservation);

    laik_switchto_partitioning(array, p0, LAIK_DF_None, LAIK_RO_None);

    Laik_Transition* transitionToP0 = 0;
    Laik_Transition* transitionToP1 = 0;
    Laik_ActionSeq* actionsToP0 = 0;
    Laik_ActionSeq* actionsToP1 = 0;

    transitionToP0 = laik_calc_transition(space, p1, p0,
                                            LAIK_DF_Preserve, LAIK_RO_None);
    transitionToP1 = laik_calc_transition(space, p0, p1,
                                            LAIK_DF_None, LAIK_RO_None);
    actionsToP0 = laik_calc_actions(array, transitionToP0, reservation, reservation);
    actionsToP1 = laik_calc_actions(array, transitionToP1, reservation, reservation);

    double * base;
    laik_get_map_1d(array, 0, (void**) &base, 0);
    if (myid == 0){
        for (long i=0; i<size; ++i)
            base[i] = (double) i;
    }

    start_time = laik_wtime();
    for (int t=0; t<trials; ++t)
    {
        // ping
        //laik_switchto_partitioning(array, p1, LAIK_DF_Preserve, LAIK_RO_None);
        laik_exec_actions(actionsToP1);
        // pong
        //laik_switchto_partitioning(array, p0, LAIK_DF_Preserve, LAIK_RO_None);
        laik_exec_actions(actionsToP0);
    }
    end_time = laik_wtime();


    if (myid == 0)
    {
        /* gnuplot ignores output lines that begin with #. */
        printf("# Length = %ld\tAverage time=%lf\n",
            size, (end_time - start_time)/(double)(2*trials));

        printf ("GB/s: %lf\n", 8.0 * 2 * trials * size  / (end_time - start_time) / (1024.0*1024*1024) );

    }



    laik_finalize(instance);
    return 0;
}

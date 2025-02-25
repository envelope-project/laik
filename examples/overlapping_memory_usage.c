#include <laik.h>
#include <assert.h>
#include <stdio.h>

// custom partitioner
void runParter(Laik_RangeReceiver* r, Laik_PartitionerParams* p)
{
    (void*) p;
    Laik_Space* space = p->space;

    int64_t size = laik_space_size(space);

    Laik_Range range_partition_0;
    laik_range_init_1d(&range_partition_0, space, 0, size);
    laik_append_range(r, 0, &range_partition_0, 0, 0);

    // add overlapping array elements
    Laik_Range range_partition_1;
    laik_range_init_1d(&range_partition_1, space, 0, (size / 2) + (size / 5));
    laik_append_range(r, 1, &range_partition_1, 0, 0);
    // add overlapping array elements
    Laik_Range range_partition_2;
    laik_range_init_1d(&range_partition_2, space, (size / 2) - (size / 5), size);
    laik_append_range(r, 2, &range_partition_2, 0, 0);
 
}

int main(int argc, char* argv[])
{
    Laik_Instance* instance = laik_init(&argc, &argv);
    Laik_Group *world = laik_world(instance);

    if (laik_size(world) != 3) {
        printf("Error: run this test with 3 processes!\n");
        laik_finalize(instance);
        exit(1);
    }

    int size = 10;

    Laik_Space* space = laik_new_space_1d(instance, size);
    Laik_Data* array = laik_new_data(space, laik_Double);

    Laik_Partitioning *p0, *p1, *p2;
    Laik_Partitioner *pr0, *pr1, *pr2;

    pr0 = laik_new_partitioner("process zero", runParter, 0, 0);
    pr1 = laik_new_partitioner("process one", runParter, 0, 0);
    pr2 = laik_new_partitioner("process two", runParter, 0, 0);

    p0 = laik_new_partitioning(pr0, world, space, 0);
    p1 = laik_new_partitioning(pr1, world, space, 0);
    p2 = laik_new_partitioning(pr0, world, space, 0);

    Laik_ActionSeq* actionsToP1 = 0;
    Laik_ActionSeq* actionsToP2 = 0;

    uint64_t count;
    // init memory of process 1
    laik_switchto_partitioning(array, p1, LAIK_DF_None, LAIK_RO_None);
    double * base_p1;
    laik_get_map_1d(array, 0, (void**) &base_p1, &count);
    for(uint64_t i=0; i < count; ++i)
        base_p1[i] = 1;

    if (laik_myid(world) == 0)
    {
        printf("Process 1 Array: [");
        for (int i = 0; i < count; ++i)
        {
            printf(" %f ", base_p1[i]);
        }
        printf("]\n");
    }

    // init memory of process 2
    laik_switchto_partitioning(array, p2, LAIK_DF_None, LAIK_RO_None);
    double * base_p2;
    laik_get_map_1d(array, 0, (void**) &base_p2, &count);
    for(uint64_t i=0; i < count; ++i)
        base_p2[i] = 2;

    if (laik_myid(world) == 0)
    {
        printf("Process 2 Array: [");
        for (int i = 0; i < count; ++i)
        {
            printf(" %f ", base_p2[i]);
        }
        printf("]\n");
    }

    laik_switchto_partitioning(array, p0, LAIK_DF_Preserve, LAIK_RO_Sum);
    double * base_p0;
    laik_get_map_1d(array, 0, (void**) &base_p0, &count);

    if (laik_myid(world) == 0)
    {
        printf("Global Array: [");
        for (int i = 0; i < size; ++i)
        {
            printf(" %f ", base_p0[i]);
        }
        printf("]\n");
    }
    laik_finalize(instance);
    return 0;
}
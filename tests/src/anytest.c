#include "laik.h"
#include <stdio.h>

int main(int argc, char* argv[])
{
    double *base;
    uint64_t count, i;

    Laik_Instance*     inst  = laik_init(&argc, &argv);
    Laik_Group*        world = laik_world(inst);
    Laik_Space*        space = laik_new_space_1d(inst, 10000);
    Laik_Data*         data  = laik_new_data(space, laik_Double);
    Laik_Partitioning* block = laik_new_partitioning(laik_new_block_partitioner1(),
                                                     world, space, 0);
    Laik_Partitioning* halo  = laik_new_partitioning(laik_new_cornerhalo_partitioner(1),
                                                     world, space, block);

    laik_switchto_partitioning(data, block, 0, 0);
    laik_fill_double(data, 1.0);
    laik_switchto_partitioning(data, halo, LAIK_DF_Preserve, LAIK_RO_Any);

    // go over own data and double each value
    laik_get_map_1d(data, 0, (void**) &base, &count);
    for (i = 0; i < count; i++) base[i] *= 2.0;

    laik_switchto_partitioning(data, halo, LAIK_DF_Preserve, LAIK_RO_Any);

    laik_get_map_1d(data, 0, (void**) &base, &count);
    for (i = 0; i < count; i++) base[i] *= 2.0;

    laik_switchto_new_partitioning(data, world, laik_Master,
                                   LAIK_DF_Preserve, LAIK_RO_Sum);
    if (laik_myid(world) == 0) {
        double sum = 0.0;
        laik_get_map_1d(data, 0, (void**) &base, &count);
        for (i = 0; i < count; i++) sum += base[i];
        printf("Result: %f\n", sum);
    }
    laik_finalize(inst);
}

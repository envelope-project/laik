#include "laik-backend-mpi.h"
#include <stdio.h>

int main(int argc, char* argv[])
{
    // use provided MPI backend, let LAIK do MPI_Init
    Laik_Instance* inst = laik_init_mpi(&argc, &argv);
    Laik_Group* world = laik_world(inst);
    // global 1d double array: 1 mio entries
    Laik_Data* a = laik_new_data_1d(inst, laik_Double, 1000000);
    // use block partitioning algorithm: equal sized, one block per process
    laik_switchto_new_partitioning(a, world, laik_new_block_partitioner1(), 0, 0);
    // parallel initialization: write 1.0 to own partition
    laik_fill_double(a, 1.0);
    // partial vector sum over own partition via direct access
    double mysum = 0.0, *base;
    uint64_t count, i;
    // map own partition to local memory space
    // (using 1d identity mapping from indexes to addresses, from <base>)
    laik_map_def1(a, (void**) &base, &count);
    for (i = 0; i < count; i++) mysum += base[i];
    // for adding the partial sums and making the result available at
    // master, first, everybody gets write access to a LAIK container
    Laik_Data* sum = laik_new_data_1d(inst, laik_Double, 1);
    laik_switchto_new_partitioning(sum, world, laik_All, LAIK_DF_None, 0);
    // write partial sum
    laik_fill_double(sum, mysum);
    // we specify that all values from writers should be added using a
    // sum reduction, with the result available at master (process 0)
    laik_switchto_new_partitioning(sum, world, laik_Master,
                                   LAIK_DF_Preserve, LAIK_RO_Sum);
    if (laik_myid(world) == 0) {
        laik_map_def1(sum, (void**) &base, &count);
        printf("Result: %f\n", base[0]);
    }
    laik_finalize(inst);
}

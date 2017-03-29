![drawing](doc/logo/laiklogo.png)

# A Library for Lightweight Application-Integrated data containers for parallel worKers

This library provides lightweight management for the distribution of global data containers for parallel applications using index spaces. It calculates the communication requirements for changing a partitioning or switching between different partitionings. By associating partitions with work load using the "owner-computes" rule, LAIK can be used for automatic load balancing.

Partitionings specify task-local access rights to be ensured when hitting a consistency point. The access rights stay valid until a consistency point with different access rights is hit. To this end, a consistency point triggers data transfers depending on a previously enforced consistency point.

Multiple partitionings with different access behavior may be declared for the same index space and the same consistency point. Hitting the point thus ensures that for each index, a value written by a previous writer gets broadcasted to all tasks which want to read the value. Reductions are supported via aggregation write behavior: multiple written values get aggregated and broadcasted to all readers when switching.

Re-partitiong (and thus load balancing) is enabled by specifically marked consistency points which require global synchronisation. In such a point, a handler is called which may change partitionings before data transfers are triggered.
LAIK-managed index spaces can be coupled to support data transfers of different data structures at the same time.

# Features

* LAIK is modularized. The core functionality is about distributed partitioning of index spaces, and declarative specification of (iterative) sequences of partitionings for these index spaces. Optional modules provide LAIK-managed data containers (coupled to index spaces), integration of communication backends, and automatic load balancing.

* Different LAIK instances may be nested to support the hierarchical topology of large HPC systems. To this end, the size of global index spaces is allowed to change. This enables partition size changes of an outer LAIK instance (e.g. responsible for distribution over cluster nodes) to be mapped to an index space of an inner LAIK instance (for intra-node distribution among CPUs and GPUs).

* LAIK provides flexible data containers for binding to index spaces, using allocator interfaces for requesting memory resources. Furthermore, applications have to acquire access to a partition, and only then the layout is fixed to some ordering of the index space to 1d memory space.

* LAIK is composable with any communication library: either the application makes direct use of provided transfer requirements in index spaces, or by coupling data containers, LAIK can be asked to call handlers from communication backends for automatic data migration. Shared memory and MPI backends are provided, but can be customized to cooperate with application code.

* LAIK enables incremental porting: data structures (and their partitioning among parallel tasks) can be moved one by one into LAIK's responsibility.

# Concepts

* LAIK's core is about partitioning of index spaces to distribute data or compute load tied to indexes among tasks. By seperating the concern of how to partition an index space from application code into LAIK, the partitioning can be controlled explicitely (e.g. by a load balancing module)

* Partitioning of index spaces may become complex, and it may be convenient to map a complex partition of one index space to a simple linear index space of the size of the partition: allows nesting, local-to-global calculations, mapping to 1d memory

  
# Example

LAIK uses SPMD (single program multiple data) programming style similar to MPI.
For the following simple example, a parallel vector sum, LAIK's communication
funtionality via repartitioning is enough. This example also shows the use of a simple LAIK data container which enables automatic data migration when switching partitioning.

```C
    #include "laik-backend-mpi.h"

    int main(int argc, char* argv[])
    {
        // use provided MPI backend, let LAIK do MPI_Init
        Laik_Instance* inst = laik_init_mpi(&argc, &argv);
        Laik_Group* world = laik_world(inst);

        // allocate global 1d double array: 1 mio entries, equal sized blocks
        Laik_Data* a = laik_alloc_1d(world, 8, 1000000);
        // parallel initialization: write 1.0 to own partition
        laik_fill_double(a, 1.0);

        // partial vector sum over own partition via direct access
        double mysum = 0.0, *base;
        uint64_t count, i;
        laik_map(a, 0, (void**) &base, &count);
        for (i = 0; i < count; i++) mysum += base[i];

        // for collecting partial sums at master, use LAIK's automatic
        // aggregation functionality when switching to new partitioning
        Laik_Data* sum = laik_alloc_1d(world, 8, 1);
        laik_set_partitioning(sum, LAIK_PT_All, LAIK_AP_Plus);
        // write partial sum
        laik_fill_double(sum, mysum);
        // master-only partitioning: add partial values to be read at master
        laik_set_partitioning(sum, LAIK_PT_Master, LAIK_AP_ReadOnly);

        if (laik_myid(inst) == 0) {
            laik_map(sum, 0, (void**) &base, &count);
            printf("Result: %f\n", base[0]);
        }
        laik_finalize(inst);
    }
```
Compile:
```
    cc vectorsum.c -o vectorsum -llaik
```
To run this example (could use vectorsum directly for OpenMP backend):
```
    mpirun -np 4 ./vectorsum
```


# License

LGPLv3+, (c) LRR/TUM

![drawing](doc/logo/laiklogo.png)

[![Build Status](https://travis-ci.org/envelope-project/laik.svg?branch=master)](https://travis-ci.org/envelope-project/laik)

# A Library for Lightweight Global Data Distribution for Parallel Applications

This library provides support for the distribution of global data for parallel applications.
It calculates the communication requirements for changing a partitioning or switching between different partitionings. By associating partitions with work load using the "owner-computes" rule, LAIK can be used for automatic load balancing.

Partitionings specify task-local accessability to be ensured. Access stay valid until a consistency point with different access rights is hit. To this end, a consistency point triggers data transfers depending on a previously enforced consistency point.

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

        // global 1d double array: 1 mio entries, equal sized blocks
        Laik_Data* a = laik_new_data_1d(world, laik_Double, 1000000);
        // parallel initialization: write 1.0 to own partition
        laik_fill_double(a, 1.0);

        // partial vector sum over own partition via direct access
        double mysum = 0.0, *base;
        uint64_t count, i;
        // map own partition to local memory space
        // (using 1d identity mapping from indexes to addresses, from <base>)
        laik_map_def1(a, (void**) &base, &count);
        for (i = 0; i < count; i++) mysum += base[i];

        // for collecting partial sums at master, we can use LAIK's data
        // aggregation functionality when switching to new partitioning
        Laik_Data* sum = laik_new_data_1d(world, laik_Double, 1);
        laik_switchto_new(sum, laik_All, LAIK_DF_ReduceOut | LAIK_DF_Sum);
        // write partial sum
        laik_fill_double(sum, mysum);
        // master-only partitioning: add partial values to be read at master
        laik_switchto_new(sum, laik_Master, LAIK_DF_CopyIn);

        if (laik_myid(world) == 0) {
            laik_map_def1(sum, (void**) &base, &count);
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

# Build and Install

There is a 'configure' command that detects features of your system and enables corresponding LAIK functionality if found:
* for MPI backend support, MPI must be installed
* for external control via MQTT, mosquitto and protobuf must be installed

To compile, run

    ./configure
    make

There also are clean, install, and uninstall targets. The install defaults
to '/usr/local'. To set the installation path to your home directory, use

    PREFIX=~ ./configure

## Ubuntu

On Ubuntu, install the following packages to enable MPI and MQTT:

    libopenmpi-dev libmosquitto-dev libprotobuf-c-dev



# License

LGPLv3+, (c) LRR/TUM

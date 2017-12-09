![drawing](doc/logo/laiklogo.png)

[![Build Status](https://travis-ci.org/envelope-project/laik.svg?branch=master)](https://travis-ci.org/envelope-project/laik)

# A Library for Automatic Data Migration in Parallel Applications

With LAIK, HPC programmers describe their communication needs on
the level of switching between program phases which have different
access behavior in regard to used data structures.

An example phase sequence involving two distributed data structures
is given in the following figure. The arrows specify for each data
structure, whether values should be preserved between old and
new phase.

![phases](doc/figs/phases.png)

Instead of explicit communication, for each program phase, a
partitioning specifies how a data structure should be distributed
among the parallel processes. Partitionings are defined on
abstract index spaces instead of offsets into real data.

![phases](doc/figs/part1.png)

Communication is triggered by the processes of the application
asking LAIK to go to the next program phase. LAIK determines
the required simple communication operations needed to fullfil
the transition to the next phase.

![transition](doc/figs/transition1.png)

The application can process the resulting actions defined on the
abstract index spaces, and map them to adequate communication operations
for used data structures. Alternatively, LAIK can allocate memory
for data attached to indexes. This way, on a switch between program
phases, LAIK can execute the communication operations itself, e.g. using MPI.

## Benefits of the LAIK Parallel Programming Model

* explicit decoupling of the specification of data decomposition
  among parallel tasks from other application code is enforced.
  This makes it easier to exchange partitioning algorithms or to
  implement dynamic re-partitioning schemes as well as external control.

* programming style becomes agnostic to the number of parallel processes:
  each process works on index ranges as specified in the partitioning
  by LAIK. This makes it easier to support elasticity, ie. dynamically
  shrinking and expanding parallel applications.

* use of LAIK data containers can significantly reduce application code
  required for explicit data communication. This makes maintance easier.
  
* the declarative style of programming allows LAIK to trigger communication
  early, enabling it to overlap communication and computation.

While similar parallel programming models exist (Charm++, Legion), LAIK
is designed to make it easy to integrate LAIK functionality step-by-step
into existing legacy HPC code written in C/C++/Fortran. At each incremental
step, the application can be tested for correctness.

If the same phase switches are done repeatedly, the required communication
operations are cached by LAIK, resulting only in a one-time initial cost
to pay for the programming abstraction. Typical HPC applications should trigger
e.g. the exact same MPI calls with LAIK than without, keeping its original
scalability.
  
# Example

LAIK uses SPMD (single program multiple data) programming style similar to MPI.
For the following simple example, a parallel vector sum, LAIK's communication
funtionality via repartitioning is enough. This example shows the use of a
simple LAIK data container.

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
        laik_switchto_new(sum, laik_All, LAIK_DF_CopyOut);
        // write partial sum
        laik_fill_double(sum, mysum);
        // master-only partitioning: add partial values to be read at master
        laik_switchto_new(sum, laik_Master, LAIK_DF_CopyIn | LAIK_DF_Sum);

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

## Installing the dependencies on Debian/Ubuntu

Packages required for minimal functionality:

    gcc make python

We currently focus on MPI support. E.g. for OpenMPI, install

    libopenmpi-dev openmpi-bin

Other packages:

    g++ libmosquitto-dev libpapi-dev libprotobuf-c-dev protobuf-c-compiler

Mosquitto and protobuf will enable external agents, and PAPI allows
to use performance counters for profiling. C++ is used in some examples.


# License

LGPLv3+, (c) LRR/TUM

Some TODOs

# Tests

* unit tests

# More examples

* matrix multiplication
* 1d/2d jacobi
* blas kernels

# Open Questions

* good allocator interface / allocation policy (for whole partitioning sequence)
* keep partitions mapped as much as possible (may be sitting on the GPU)
* Data Pipelining (SLURM, 12GB Matrix)

# Finish half implemented features

Driven by example code we want to work (see above)

* more data types
* couple index spaces / partitionings

# Refactoring and design

* split acess/data flow from partitiong

# Features not yet implemented

* Get Partition-Number/Name by HostName
* stencil example (with derived Halo partitioning)
* OpenMP/PThreads backend
* LAIK nesting (using MPI / PThreads)
* OpenCL backend
* interface for application-specific partitioners
* control from outside (MQTT)
* ...

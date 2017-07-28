# Some TODOs

Priorities (in parenthesis):
* 1: high
* 2: medium
* 3 low

## Misc

* trigger repartitioning via task group modification (1)
* merge consecutive slices after repartitioning (2)
* ...


## Tests

* unit tests

## More examples

* matrix multiplication
* 1d/2d jacobi
* blas kernels

## Open Questions

* good allocator interface / allocation policy (for whole partitioning sequence)
* how to keep partitions on GPU
* Data Pipelining (SLURM, 12GB Matrix)

## Finish half implemented features

Driven by example code we want to work (see above)

* more data types
* couple index spaces / partitionings

## Refactoring and design

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

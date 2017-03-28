Some TODOs

# Tests

* start: use the debug output from examples with different task counts
* add a way to "normalize" ordering in debug output (allow piping into "sort")
* set up Travis

# More examples

* matrix multiplication
* 1d/2d jacobi
* blas kernels

# Open Questions

* good allocator interface / allocation policy (for whole partitioning sequence)
* keep partitions mapped as good as possible (may be sitting on the GPU)
* ... ?

# Finish half implemented features

Driven by example code we want to work (see above)

* list of slices in a partition
* more data types
* couple index spaces / partitionings
* ...

# Features not yet implemented

* stencil example (with derived Halo partitioning)
* OpenMP/PThreads backend
* LAIK nesting (using MPI / PThreads)
* OpenCL backend
* interface for application-specific partitioners
* control from outside (MQTT)
* ...

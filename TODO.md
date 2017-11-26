# API Proposals and SW Engineering Process

Keep track of open issues and needed changes here,
with references (e.g. Github issue number / bug tracker),
as basis for changelog.

Labels used:
* (1) high priority - must be in next release
* (2) medium priority - nice to have in next release
* (3) low priority
* (API) API related
* (SE) SW engineering related
* (DONE): resolved, kept for writing changelog for next release


## Releases

Next: 0.1, January 18
* efficient API, works for Jacobi examples, LULESH port

Upcoming stable: 1.0, April 18
* API review
* better separation of modules
* works with more ported apps (NAS)


## API Proposals

### Allocation (1, API)
- data reallocation never should happen in regular main loop of apps
- laik_data_reserve(data, partitioning): switching to given partitioning does not need allocation
- plus: no need for laik_map after every switch
- plus: no allocation triggered by backend driver
- plus: more predictable memory requirements
- minus: needs separate API to wait for asynch data after switch (currently this is expected to be a part of laik_map)

### Mapping
* [discussion](docs/design/Mapping.md)



## Implementation

* trigger repartitioning via task group modification (1)
* merge consecutive slices after repartitioning (2)
* crash with "mpirun -np 2 example/spmv2 -r": reduce not over all tasks (1)


## Tests

* integration tests for implemented LAIK funtionality (1, SE)
  - separate examples (simplify!) from integration tests
* unit tests (2, SE)

## More examples

* matrix multiplication
* 1d/2d jacobi (DONE)
* blas kernels


## Open Questions (integrate above)

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

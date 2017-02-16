# LAIK - A Library for Lightweight Automatic Integrated data containers for parallel worKers

This library provides lightweight data containers for parallel applications with support
for automatic load-balancing. Before working on structure elements, the parallel workers
within an applications are expected to ask the library for owned partitions
("owner-computes"). Eventual re-partitioning is driven by element-wise weighting, and
is executed at the next logical barrier when load imbalance surpasses a given threshould.

# Features

* composable with any communication library: LAIK works with arbitrary communication backends, providing the required communication functionality. Some standard backends are provided, but can be customized to cooperate with the existing application code.
* enables incremental porting: data structures (and their partitioning among parallel tasks) can be moved one by one into LAIK's responsibility. LAIK can be instructed to not allocated memory resources itself, but use custom hooks for application provided allocators.


# API

laik_init()

laik_malloc()

laik_mypartition()

laik_barrier()

laik_setweight()

laik_repartition()

laik_finish()


## Example

TBD

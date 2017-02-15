# laik - A Library for Lightweight Automatic Integrated data containers for parallel worKers

This library provides lightweight data containers for parallel applications with support
for automatic load-balancing. Before working on structure elements, the parallel workers
within an applications are expected to ask the library for owned partitions
("owner-computes"). Eventual re-partitioning is driven by element-wise weighting, and
is executed at the next logical barrier when load imbalance surpasses a given threshould.

## API

laik_init()

laik_malloc()

laik_mypartition()

laik_barrier()

laik_setweight()

laik_repartition()

laik_finish()


## Example

TBD

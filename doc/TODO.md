# Open Issues

## API Design

* we should separate partitioning from access behavior because
  - if partitioning is directly used without data, access behavior makes no sense
  - if two data structures want to use the same partitioning, it is strange to "copy" from an struct specifying another access behavior

* at points calling allowRepartition, LAIK needs to copy data around
  - the application may want to specify access behaviour before and after for all used LAIK data, to not uselessly need data transfers
  - API: if data is not needed / not written to yet, we should mark that
  - enforcing a consistency-point could specify all kind of transitions of access behaviour, and include an allowed repartitioning. To restrictive?

* allowRepartitioning may need to change all partitionings (to free compute nodes) or partitioning-specific (load balancing just influencing one partitioning)

* How to specify different access behavior for different parts of a LAIK partitioning?


## LAIK-internal design

* get rid of Laik_Space for now? Could be replaced by a PSpace (partitioned space)

* callback interface to broadcast partitioning changes = transitions to (1) the application directly, or (2) to LAIK data objects using the partitioning

* when a partitioning struct is modifed from external source, it still is the same partitioning struct. To calculate transistions, use separate calculated borders (before/after)

* cache transitions

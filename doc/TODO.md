# Open Issues

## Layouts

* generic unpack
* allow custom layout factory
* use one layout for all mappings of a container
* modify jac3d to use custom layout

## API Design

* terms
  - "index space" = Laik_Space: correct term?
    better "index domain"? ... no need, should be fine
  - "slice" (Laik_Slice)
    - this is "just" a consecutive range in an index space
    - usually, the term "slice" is used in languages/libraries with data backing
    better "range"

* we should separate partitioning from access behavior because [DONE]
  - if partitioning is directly used without data, access behavior makes no sense
  - if two data structures want to use the same partitioning, it is strange to "copy" from an struct specifying another access behavior

* at points calling allowRepartition, LAIK needs to copy data around
  - the application may want to specify access behaviour before and after for all used LAIK data, to not uselessly need data transfers
  - API: if data is not needed / not written to yet, we should mark that
  - enforcing a consistency-point could specify all kind of transitions of access behaviour, and include an allowed repartitioning. To restrictive?
  - DONE: we derive fitting data flow from existing, and allowRepartition switches to new phase

* allowRepartitioning may need to change all partitionings (to free compute nodes) or partitioning-specific (load balancing just influencing one partitioning)

* How to specify different access behavior for different parts of a LAIK partitioning?
  - cannot be done. No use case (yet).

* a data container does not need to relate to a task group
  - if no partitioning is active, no data can exist => no group necessary
  - relationship between data and group exist whenever a partitioning (using a
    group) is active

* can the creation of space/partitioning/data always be a collective operation?
  - all tasks need to know about them, even if they are not currently part
    of a task group involved in using the space/partitioning/data
  - how identified? Currently by "collective" creation in same order
  - what about new coming processes?


## LAIK-internal design

* get rid of Laik_Space for now? Could be replaced by a PSpace (partitioned space)

* callback interface to broadcast partitioning changes = transitions to (1) the application directly, or (2) to LAIK data objects using the partitioning

* when a partitioning struct is modifed from external source, it still is the same partitioning struct. To calculate transistions, use separate calculated borders (before/after)

* cache transitions

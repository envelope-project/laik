# LAIK 👍 - A Library for Lightweight Automatic Integrated data containers for parallel worKers

This library provides lightweight data containers for parallel applications with support for dynamic re-distribution with automatic data migration. The data distribution specifies which data locally is available for direct access by tasks, enabling the "owner-computes" rule. Load-balancing is enabled by explicit repartitioning using element-wise weights.

# Features

* LAIK is composable with any communication library: LAIK works with arbitrary communication backends, expected to provide the required communication functionality. Some standard backends are provided, but can be customized to cooperate with the existing application code.

* LAIK enables incremental porting: data structures (and their partitioning among parallel tasks) can be moved one by one into LAIK's responsibility. LAIK can be instructed to not allocate memory resources itself, but use custom hooks for application provided allocators/data layouts.

  
# Example

LAIK uses SPMD (single program multiple data) programming style similar to MPI.
For the following simple example, a parallel vector sum, LAIK's communication
funtionality via repartitioning is enough.

    #include "laik.h"
   
    main() {
      laik_init(laik_tcp_backend); // use TCP backend
     
      // allocate global 1d double array: 1 mio entries, equal sized stripes
      Laik_Data double_arr = laik_alloc(LAIK_1D_DOUBLE, 1000000, LAIK_STRIPE);
      // parallel initialization: write 1.0 to own partition
      laik_fill_double(double_arr, 1.0);

      // partial vector sum over own partition via direct access
      uint64_t i, count;
      double mysum = 0.0, *base;
      laik_mydata(double_arr, &base, &count);
      for(i=0; i<count; i++) { mysum += base[i]; }
     
      // for collecting partial sums at master, use LAIK's automatic
      // data migration when switching between different partitionings;
      Laik_DS sum_arr = laik_alloc(LAIK_1D, laik_size(), sizeof(double), LAIK_STRIPE);
      // set own sum_arr value to partial sum
      laik_fill_double(sum_arr, mysum);
      // switch to master-only partitioning: this transfers values to master
      laik_repartition(sum_arr, LAIK_SINGLEOWNER, 0);
     
      if (laik_myid() == 0) {
        double sum = 0.0;
        laik_mydata(sum_arr, &base, &count);
        for(i=0; i<count; i++) { sum += base[i]; }
        print("Result: %f\n", sum);
      }
      laik_finish();
    }
   
Compile:

    cc vectorsum.c -o vectorsum -llaik

To run this example, the LAIK's TCP backend is supported by the LAIK launcher "laikrun":

    laikrun -h host1,host2 ./vectorsum


# Concepts

* LAIK manages the distribution of global data between a group of tasks
  of a parallel program via LAIK **containers**. Each container holds
  a fixed number of elements of the same type, specified at creation time.

* For each container, multiple **partitionings** can be defined with at most
  one partitioning active at any point in time. The active partitioning
  specifies which local access is allowed/enabled for tasks in the group.
  For each element, at most one task may have write permission, but multiple
  tasks may have read permission. The task with write access is the element owner.

* Every element in a container has a global index. For direct access to
  container elements via memory addresses, a partition has to be **pinned**.
  Only at this point, the memory ordering of locally accessable elements
  via a local index has to be defined. If a given order (mapping from local
  to global indexes) is needed, it can be specified at pinning time, but
  may result in time-consuming layout conversion. Alternatively, LAIK may
  decide about the best ordering, providing the used index order. Often,
  the order mapping can be provided as simple function (e.g. simply adding
  an offset, or reshuffling a multi-dimensional indexes) which helps
  performance if data traversals use special code for simple mappings.

* **Communication** is triggered by enforcing consistency for a container in
  regard to the active partitioning, which ensures that element values are
  copied from owner tasks to tasks with read access. Enforcing consistency
  is independent from switching between partitionings, which just changes
  ownership and does not need data transfers. However, consistency
  may be enforced atomically together with a switch of the partitioning.
  LAIK enables asynchronous communication by starting required data transfers
  at partitioning switch time, but only requires the data to have arrived
  at the destination task when the task wants to pin it to memory.
  To reduce communication, LAIK allows to pin owned memory read-only by
  default, with explicit notification if elements were written to. Furthermore,
  any read-only pinning is not enforced (e.g. via MMU) but is expected to
  not be written to. LAIK tries to reuse memory pinnings, and may not update
  values if it expects them to still be correct.
  
* Changing/adding/deleting partitiongs for a container requires global
  **synchronisation** among the tasks in the task group that has access
  to the container. Partitioning changes are useful for balancing work
  load that depends on the size of partitions in a partitioning.
  Active partitionings cannot be changed. However, an active partitioning
  may be cloned into a new version, with the clone being changed.
  Switching to the new versions will remove the old version of the
  partitioning. 
  As long as partitiongs stay fixed, a switch from one to
  another only results in local synchronisation among the tasks which
  need to send data to each other.

* A computational kernel usually wants to access data from multiple
  containers. To this end, containers can be **coupled** by specifying
  a coupling scheme for global indexes. Switching the partitioning
  of a container automatically also switches the partitiong of coupled
  containers.


# Most important API functions

## Laik_Error* laik_init( Laik_Backend* backend )

Initialize LAIK, using *backend* for communication.
Backend parameters usually are passed via environment variables.
On success, *laik_init* returns NULL, otherwise an error struct.

## Laik_Data laik_alloc( Laik_Type t, uint64_t c, Laik_Part p)

Creates a handle for global, LAIK-managed data of type *t*
with *c* elements and *p* as initially active partitioning.
(TODO: task group).

## Laik_Pinning laik_mydata( Laik_Data d, Laik_Order o)

Pins owned partition of active partitioning of *d* to local memory,
taking order wish *o* into account, and returning the used
pinning order. This includes the base address of the pinning and
the number of pinned elements.

## laik_repartition( Laik_data d, Laik_Part p)

Defines a new partitioning, and makes it active.

## laik_finish()

Shutdown the communication backend.

# LAIK 👍 - A Library for Lightweight Automatic Integrated data containers for parallel worKers

This library provides lightweight management for the distribution of global data containers for parallel applications using index spaces. It calculates the communication requirements for changing a partitioning or switching between different partitionings. By associating partitions with work load using the "owner-computes" rule, LAIK can be used for automatic load balancing.

Partitionings specify task-local access rights to be ensured when hitting a consistency point. The access rights stay valid until a consistency point with different access rights is hit. To this end, a consistency point triggers data transfers depending on a previously enforced consistency point.

Multiple partitionings with different access permissions may be declared for the same index space and the same consistency point. Hitting the point thus ensures that for each index, a value written by a previous writer gets broadcasted to all tasks which want to read the value. Reductions are supported via aggregation write permissions: multiple written values get aggregated and broadcasted to all readers when switching.

Re-partitiong (and thus load balancing) is enabled by specifically marked consistency points which require global synchronisation. In such a point, a handler is called which may change partitionings before data transfers are triggered.
LAIK-managed index spaces can be coupled to support data transfers of different data structures at the same time.

# Features

* LAIK is modularized. The core functionality is about distributed partitioning of index spaces, and declarative specification of (iterative) sequences of partitionings for these index spaces. Optional modules provide LAIK-managed data containers (coupled to index spaces), integration of communication backends, and automatic load balancing.

* Different LAIK instances may be nested to support the hierarchical topology of large HPC systems. To this end, the size of global index spaces is allowed to change. This enables partition size changes of an outer LAIK instance (e.g. responsible for distribution over cluster nodes) to be mapped to an index space of an inner LAIK instance (for intra-node distribution among CPUs and GPUs).

* LAIK provides flexible data containers for binding to index spaces, using allocator interfaces for requesting memory resources. Furthermore, applications have to acquire access to a partition, and only then the layout is fixed to some ordering of the index space to 1d memory space.

* LAIK is composable with any communication library: either the application makes direct use of provided transfer requirements in index spaces, or by coupling data containers, LAIK can be asked to call handlers from communication backends for automatic data migration. Shared memory and MPI backends are provided, but can be customized to cooperate with application code.

* LAIK enables incremental porting: data structures (and their partitioning among parallel tasks) can be moved one by one into LAIK's responsibility.

  
# Example

LAIK uses SPMD (single program multiple data) programming style similar to MPI.
For the following simple example, a parallel vector sum, LAIK's communication
funtionality via repartitioning is enough. This example also shows the use of a simple LAIK data container which enables automatic data migration when switching partitioning.

```C
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
```
Compile:
```
    cc vectorsum.c -o vectorsum -llaik
```
To run this example, the LAIK's TCP backend is supported by the LAIK launcher "laikrun":
```
    laikrun -h host1,host2 ./vectorsum
```


# Important API functions

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

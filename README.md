# LAIK - A Library for Lightweight Automatic Integrated data containers for parallel worKers

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
      // use TCP backend, supported by "laikrun" launcher
      laik_init(laik_tcp_backend);
     
      // allocate dense double array with 1 mio entries, inititial value 1.0,
      // use weighted stripe partitioning with element weight 1.0
      laik_ds double_arr = laik_alloc(LAIK_1D_Double, 1000000, 1.0, LAIK_STRIPE, 1.0);

      // sum up values within my partition
      double *ptr, *end, mysum = 0.0;
      laik_mypartition(double_arr, &ptr, &end);
      for(; ptr != end; ptr++)
        mysum += *ptr;
     
      // for collecting partial sums at master, use LAIK's automatic
      // data migration when switching between different partitionings;
      // every task gets exactly one element initialized with his partitial sum
      laik_ds sum_arr = laik_alloc(LAIK_1D_Double, laik_size(), mysum, LAIK_STRIPE, 1.0);
     
      // switch to partitioning with only master having a non-zero weight;
      // this transfers all values to master
      laik_repartition(sum_arr, LAIK_STRIPE, (laik_myid() == 0) ? 1.0 : 0.0);
     
      if (laik_myid() == 0) {
        double sum = 0.0;
        laik_mypartition(sum_arr, &ptr, &end);
        for(; ptr != end; ptr++)
          sum += *ptr;
        print("Result: %f\n", sum);
      }
      laik_finish();
    }
   
Compile:

    cc vectorsum.c -o vectorsum -llaik

To run this example, the LAIK's TCP backend is supported by the LAIK launcher "laikrun":

     laikrun -h host1,host2 ./vectorsum


# API

laik_init()

laik_alloc()

laik_mypartition()

laik_repartition()

laik_finish()


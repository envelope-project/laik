# LAIK Overview

With LAIK, HPC programmers describe their communication needs on
the level of switching between access phases to data structures
using different partitionings of global index spaces. For example,
the phase sequence involving two distributed data structure may
look like that:

![phases](figs/phases.png)

Furthermore, a specification of a partitioning, that is, how indexes
in an abstract index space are assigned to processes, is shown here:

![phases](figs/part1.png)

This results in coarse-grained communication triggered at phase
transitions, consisting of simple communication actions, automatically
identified by LAIK:

![transition](figs/transition1.png)

The application can map the actions on an abstract index space
to adequate communication operations for used data structures
itself, or it can ask LAIK to manage memory attached to indexes.
This way, on a switch between program phases, LAIK executes
the required data communication itself, e.g. using MPI.

Benefits of the LAIK parallel programming model:

* explicit decoupling of the specification of data decomposition
  among parallel tasks from application code specifying the computation.
  This makes it easier to exchange partitioning algorithms or to
  implement dynamic re-partitioning schemes as well as external control.

* programming style becomes agnostic to the number of parallel processes:
  each process works on index ranges as specified in the partitioning
  by LAIK. This makes it easier to support elasticity, ie. dynamically
  shrinking and expanding parallel applications.

* using LAIK data containers can significantly reduce application code
  required for explicit data communication. This makes maintance easier.

* the declarative style of programming allows LAIK to trigger communication
  early, enabling it to overlap communication and computation.

While similar parallel programming models exist (Charm++, Legion), LAIK
is designed to make it easy to integrate LAIK functionality step-by-step
into existing legacy HPC code written in C/C++/Fortran. At each incremental
step, the application can be tested for correctness.

If the same phase switches are done repeatedly, the required communication
operations are cached by LAIK, resulting only in a one-time initial cost
to pay for the programming abstraction. Typical HPC applications should trigger
e.g. the exact same MPI calls with LAIK than without, keeping its original
scalability.

# Elasticity for HPC applications

Parallel HPC applications usually are written in a low-level parallel programming model, such as MPI. If computational demands of the application are dynamic, adaptive load balancing has to be implemented by the programmer explictly. The resulting code often is application-specific and quite complex, making future extensions and maintainability tricky. With components used in future HPC systems to become more heterogenous and with variable computational capacity, HPC applications are expected to require load balancing for high scalability.

When HPC applications use LAIK, the library takes over control of the partitioning of data. LAIK is meant for applications written according to the owner-computes rule, where computational demand depends on the size of data partitions given to the processes/threads running in parallel. By controlling partitioning, LAIK provides dynamic load balancing. Furthermore, LAIK provides elasticity. While an application is running, LAIK can remove work load from nodes, allowing them to be taken down for maintainance (e.g. if predicted to fail soon), and it can make use of new resources added on-the-fly.

# The LAIK Programming Model

Programmers have to give LAIK enough information such that it can redistribute data itself. For best performance, applications should have any data locally available that is needed to do the computation for the partition given by LAIK. To this end, applications are split into data access phases, and programmers explicitly specify data dependencies.

LAIK does not directly partition data, but uses *index spaces* as abstraction of data items to work on. While LAIK controls the partitioning of such abstract index spaces, the programmer defines the shape of the index spaces as well as the mapping from indexes to actual data to work on. This way, a change in a partititioning can be seen as two steps. First, the change in the index space results in transition actions defined on indexes: which indexes stay local, which need to be communicated? From this, changes in the actual memory allocation and required communication can be derived.

LAIK controls partitioning in the sense that it triggers a re-run of a partitioning algorithm when required, such as when weights given as input to the partitioning algorithm change, or when processes are removed or added. The partitioning algorithm itself often is application-specific and needs to be provided by the application. Data dependencies are also specified as so-called *derived* partitioning algorithms: e.g. for Jacobi, to update cells within a region, one must have access to the halo of the region. Calculating such extended regions is the job of an partitioning algorithm given by the programmer.

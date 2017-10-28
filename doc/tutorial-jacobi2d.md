# Jacobi 2D Example

In this tutorial, we will look at how a simple iterative solver for solving the Poisson equation on a regular 2d quadratic discretized domain is parallelized with LAIK. C is used as programming language.

## Sequential Version

For this example, we want to find the static temperature distribution on a 2d quadratic metal plate, discretized by N times N cells (using a 2d matrix `double v[N][N]`), with the temperatures set to given values at the plate edges (fixed boundary condition). To get the solution for inner values, we start with an initial approximation (e.g. 0.0), and per iteration, we update cell values into a temporary matrix `vtemp` by averaging the values of neighbor cells:

    vtemp[y][x] = 0.25 * (v[y-1][x] + v[y+1][x] +
                          v[y][x-1] + v[y+1][x+1])

After each traversal, `vtemp` is interpreted as the new approximation of matrix `v`, and we stop if the distance (L2 norm) between `v` and `vt` falls below a threshold. For simplicity, in this tutorial, we do a fixed number of `MAXITER` iterations, assumed to be even, and use matrixes `v1` and `v2`, alternatively being the current approximations of the solution. 

The sequential C code looks like this:

```C
#define SIZE 10000
#define MAXITER 100

int main() {
  double v1[SIZE][SIZE], v2[SIZE][SIZE];
  int iter, x, y;

  // set fixed boundaries both in v1 and v2,
  // write initial approximation into v1
  
  for(iter = 0; iter < MAXITER; iter += 2) {
    // update v2 cells from v1
    for(y = 1; y < SIZE-1; y++)
      for(x = 1; x < SIZE-1; x++)
        v2[y][x] = .25 * (v1[y-1][x] + ...);

    // update v1 cells from v2
    for(y = 1; y < SIZE-1; y++)
      for(x = 1; x < SIZE-1; x++)
        v2[y][x] = .25 * (v1[y-1][x] + ...);
  }

  // print solution
}
```

## Parallelization Approach

We note that there is no dependency between updates of cell values in one iteration. Thus, we traversal over all cells could be done in any order. Thus, to parallelize the updates of one iteration, we can distribute all cells of v1 (and v2) among the parallel tasks, which only then do the updates on the cells they "own". To balance the work load, every task has to own the same number of nodes.

For example, with `SIZE = 100` and 4 tasks, we can split the matrix into 4 quadrants, each having a size of `49x49` (with fixed boundaries, only the inner part of size `98x98` has to be updated).

To actually do the updates, we need the values of neighboring cells. That means that for a task responsible for updating the cells within a given rectangle `(x1/y1)-(x2/y2)`, we need values from a so-called halo region with depth 1 around the "owned" rectangle. Assuming each task to use its own memory, with a message passing programming model (eg. MPI), after each update, communication needs to be done to update the halo regions (if the rectangle of cells assigned to a task touches the fixed boundary, of course, no communication needs to be done for this edge).


## Parallelization with LAIK

LAIK programs follow the SPMD (Single Program Multiple Data) style, similar to MPI. That is, your program is launched multiple times as separate processes, eventually on different machines. A program start procedure depending on the communication backend (e.g. `mpirun` if LAIK uses MPI behind the scenes) and a call for LAIK initialization in the code together form the initial LAIK task group we call the world group. During execution, this world group may shrink and expand.

With LAIK, a programmer first has to decribe the parallel program in terms of used data structures and how they are accessed within access phases. Afterwards, the program switches between theses phases, and LAIK makes sure that data for computation is locally available as declared before.

### Initialization

LAIK relies on so-called "communication backends" to do any communication or synchronization as needed among LAIK tasks. Multiple communication backends can be used at the same time assigned to different active LAIK instances. First, we need to initialize a LAIK instance coupled to a given backend, eg. MPI (if you already called `MPI_Init`, this is not done by passing `(0,0)` as arguments to `laik_init_mpi`). We also ask LAIK for the `world` object representing all tasks available to this instance. 

```C
#include "laik-mpi.h"

#define SIZE 10000
#define MAXITER 100

int main(int argc, char* argv[])
{
  Laik_Instance* inst;
  inst = laik_init_mpi(&arg, &argv);
  Laik_Group* world = laik_world(inst);
  ...
```

If we would use MPI, we would explicitly specify the partitioning of matrix cells to each MPI task. With LAIK, we give away the concern of partitioning of data among tasks to LAIK. This allows LAIK to do a re-partitioning whenever there is a request to shrink or enlarge the number of tasks, which is the single purpose of LAIK (enlarging means creating new processes by starting new instances of your program; you can ask a LAIK instance for a program phase counter, and directly jump to the corresponding code after initialization; we show this later).

### Index Spaces and Data Management

For 2d Jacobi, we first declare an index space whose shape fits our problem. Here, the most straight-forward approach is to map each cell of the matrix v1 or v2 to an index.

```C
Laik_Space* space;
space = laik_new_space_2d(inst, SIZE, SIZE);
```

We can use LAIK by just working with index spaces and do the management of memory resources for our data structures on our own. However, here we show how to ask LAIK to do the allocation of v1 and v2 for us. Each index space should be coupled with a `double` value:

```C
Bla
```

### Program Phases and Data Flows

The programmer must make LAIK aware of program phases, where a phase has a specific access pattern to data structures of the program (such as reading or writing). To enable LAIK to specify a partitioning of data, LAIK works with abstract index spaces and assignment of indexes within these spaces to tasks. The programmer then maps indexes to different elements of used data structures. Finally, LAIK must be made aware of the data that has to be locally accessable to an task for the task to work on its owned indexes.

```C
Bla
```

### Putting it All Together

Bla

## Summary

In this tutorial, we saw how an iterative Jacobi solver is implemented with LAIK. For details, there are 1d/2d/3d versions of such a solver in the `examples/` subdirectory in the LAIK sources. 

### Items Covered

* specification of index spaces
* partitioning of index spaces, coupling of partitionings
* containers: data binding to index spaces
* access phases into data containers
* automatic repartitioning at phase ends

### Where to go from here

Now that you know the basic structure of LAIK programs, you can read about

* the various supported index spaces.
* types of data containers, and
* how to write your own partitioner.

The latter is important for any more complex application, as resulting partitionings actually describe the indexes (and corresponding data) which need to be locally available in different phases, as well as implicitly contain the knowledge about work load resulting from different partition sizes. Both is usually specific to each application.

More advanced topics:
* more complex data binding to indexes
* defining your own data layout mapping indexes to 1d memory address space
* enabling asynchronous computation and communication
* using LAIKs key-value store for application data
* nesting of LAIK instances to match HPC system topologies
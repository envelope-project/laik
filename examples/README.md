# LAIK Examples

Currently, most of example applications also are used as
integration test, and thus more complicated than useful for
examples. This will change in the future.

## Simple examples

* vsum, vsum2: vector sum, basic example of how to use LAIK to do communication
* raytracer: use LAIK to collect results at master

## SPMV Examples

* spmv
* spmv2

## Jacobi Examples

Iterative solvers, using two data structures.
In each iteration, elements in one data structure are updated
from values in the other data structure.

* jac1d
* jac2d
* jac3d

## Markov Chain Examples

These use a graph connectivity to specify the statistical relationship
between states. Starting from initial probabilities of nodes, the static
distribution is approximated in iterations, taking the values at predecessor
states into account. This uses a very fine-graied partitioner, stress testing
the partitioning (slice) processing.

* markov: tasks are responsible for updating the probability of owned states
from predecessors
* markov-ser: sequential version of markov, for validating results
* markov2: tasks own source states, which propagate their values to successors,
with reduction done by LAIK for states at task boundaries.


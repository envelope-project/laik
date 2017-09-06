# TODOs for examples

## jac2d

* current halo partitioner triggers needless 1-element communication at corners, to diagonal neighbors,
  ie. on regular grids 8 instead of 4 neighbors (for inner tasks). 

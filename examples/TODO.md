# TODOs for examples

## jac2d/jac3d

* put fixed boundaries in a LAIK container coupled to the tasks which have the matrix elements
* current halo partitioner triggers needless 1-element communication at corners, to diagonal neighbors,
  ie. on regular grids 8 instead of 4 neighbors, for inner tasks. Needs some LAIK support for not
  resulting in multiple mappings for each halo side (=> another example, with multiple layouts)

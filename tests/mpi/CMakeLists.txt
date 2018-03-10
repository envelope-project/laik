get_target_property (definitions "laik" "COMPILE_DEFINITIONS")

if ("USE_MPI" IN_LIST definitions)
    foreach (test
        "test-jac1d-1000-repart-mpi-1.sh"
        "test-jac1d-1000-repart-mpi-4.sh"
        "test-jac1d-100-mpi-1.sh"
        "test-jac1d-100-mpi-4.sh"
        "test-jac2d-1000-mpi-1.sh"
        "test-jac2d-1000-mpi-4.sh"
        "test-jac2dn-1000-mpi-4.sh"
        "test-jac3d-100-mpi-1.sh"
        "test-jac3d-100-mpi-4.sh"
        "test-jac3dn-100-mpi-4.sh"
        "test-jac3dr-100-mpi-1.sh"
        "test-jac3dr-100-mpi-4.sh"
        "test-markov-20-4-mpi-1.sh"
        "test-markov2-20-4-mpi-1.sh"
        "test-markov2-40-4-mpi-4.sh"
        "test-markov-40-4-mpi-4.sh"
        "test-propagation2d-10-mpi-1.sh"
        "test-propagation2d-10-mpi-4.sh"
        "test-spmv2-mpi-1.sh"
        "test-spmv2-mpi-4.sh"
        "test-spmv2r-mpi-1.sh"
        "test-spmv2r-mpi-4.sh"
        "test-spmv2-shrink-inc-mpi-4.sh"
        "test-spmv2-shrink-mpi-4.sh"
        "test-spmv-mpi-1.sh"
        "test-spmv-mpi-4.sh"
        "test-vsum2-mpi-1.sh"
        "test-vsum2-mpi-4.sh"
        "test-vsum3-mpi-1.sh"
        "test-vsum3-mpi-4.sh"
        "test-vsum-mpi-1.sh"
        "test-vsum-mpi-4.sh"
    )
        add_test ("mpi/${test}" "${CMAKE_CURRENT_SOURCE_DIR}/${test}")
    endforeach ()
endif ()

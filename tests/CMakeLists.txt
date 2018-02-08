# Run the example tests for all enabled backeds
foreach (test
    "jac1d;100"
    "jac1d;1000;50;10"
    "jac2d;-s;1000"
    "jac2d;-s;-n;1000"
    "jac3d;-s;100"
    "jac3d;-s;-n;100"
    "markov2"
    "markov;20;4"
    "markov2;20;4"
    "propagation2d;10;10"
    "spmv;1000"
    "spmv2;10;3000"
    "spmv2;-r;10;3000"
    "spmv2;-s;2;10;3000"
    "spmv2;-s;2;-i;10;3000"
    "spmv;4000"
    "vsum"
    "vsum2"
    "vsum3"
)
    # Determine a suitable test name automatically
    string (REPLACE ";" "_" name "${test}")

    if (enable-mpi-backend)
        add_test (
            NAME    "mpi_${name}"
            COMMAND "env" "LAIK_BACKEND=mpi" "${CMAKE_CURRENT_SOURCE_DIR}/run_example_test.sh" ${test}
        )
    endif (enable-mpi-backend)

    if (enable-single-backend)
        add_test (
            NAME    "single_${name}"
            COMMAND "env" "LAIK_BACKEND=single" "${CMAKE_CURRENT_SOURCE_DIR}/run_example_test.sh" ${test}
        )
    endif (enable-single-backend)
endforeach (test)

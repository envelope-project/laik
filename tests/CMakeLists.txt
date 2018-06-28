foreach (test
    "test-jac1d-1000-repart-single.sh"
    "test-jac1d-100-single.sh"
    "test-jac2d-1000-single.sh"
    "test-jac3d-100-single.sh"
    "test-jac3dr-100-single.sh"
    "test-markov-20-4-single.sh"
    "test-markov2-20-4-single.sh"
    "test-propagation2d-10-single.sh"
    "test-spmv2r-single.sh"
    "test-spmv2-single.sh"
    "test-spmv-single.sh"
    "test-vsum2-single.sh"
    "test-vsum-log-single.sh"
    "test-vsum-single.sh"
)
    add_test ("single/${test}" "${CMAKE_CURRENT_SOURCE_DIR}/${test}")
endforeach ()

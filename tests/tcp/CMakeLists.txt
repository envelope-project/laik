get_target_property (definitions "laik" "COMPILE_DEFINITIONS")

if ("USE_TCP" IN_LIST definitions)
    foreach (test
        "jac1d_1000_50_10.sh"
        "jac1d_100.sh"
        "jac2d_-s_1000.sh"
        "jac2d_-s_-n_1000.sh"
        "jac3d_-r_-s_100.sh"
        "jac3d_-s_100.sh"
        "jac3d_-s_-n_100.sh"
        "markov2_40_4.sh"
        "markov_40_4.sh"
        "propagation2d_10_10.sh"
        "spmv2_10_3000.sh"
        "spmv2_-r_10_3000.sh"
        "spmv2_-s_2_10_3000.sh"
        "spmv2_-s_2_-i_10_3000.sh"
        "spmv_4000.sh"
        "vsum2.sh"
        "vsum.sh"
    )
        add_test ("tcp/${test}" "${CMAKE_CURRENT_SOURCE_DIR}/${test}")
    endforeach ()
endif ()

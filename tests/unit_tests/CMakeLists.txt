foreach (unit_test
#        "core"
        "checkpoint"
)
    add_executable("${unit_test}" "${CMAKE_CURRENT_SOURCE_DIR}/${unit_test}.c")

    target_link_libraries ("${unit_test}"
            PRIVATE "laik"
            PRIVATE "m"
            )

    set (test_parameters -v -n 4 -disable-recovery "${CMAKE_CURRENT_BINARY_DIR}/${unit_test}")
    add_test(NAME ${unit_test} COMMAND "mpirun" ${test_parameters})

#    set_tests_properties(
#            ${unit_test}
#            PROPERTIES
#            PASS_REGULAR_EXPRESSION
#            "Test passed"
#    )
endforeach ()

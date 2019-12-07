foreach(integration_test
        "./test-jac2d-1000-ft.sh"
        )

    add_test(NAME ${integration_test} COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/${integration_test}" "../../tests/fault-tolerance" "${CMAKE_CURRENT_BINARY_DIR}")
endforeach()

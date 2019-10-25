# Build the C examples
foreach (test
        "checkpoint-jac2d"
        "checkpoint-jac2d-recovery"
        "checkpoint-partitioner"
        "checkpoint-mandelbrot-recovery"
        )

    add_executable ("${test}"
            "${test}.c"
            "util/fault-tolerance-options.h"
            "util/fault-tolerance-options.c"
            )

    target_link_libraries ("${test}"
            PRIVATE "laik"
            PRIVATE "m"
            )
endforeach ()


foreach(integration_test
        "test-jac2d-1000-ft.sh"
        "test-lulesh-ft.sh" )

    add_test(NAME ${integration_test} COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/${integration_test}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
endforeach()

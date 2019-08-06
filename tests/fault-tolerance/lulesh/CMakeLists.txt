# Build the C examples
foreach (test
        "lulesh"
        )

    add_executable ("${test}"
            "${test}.cc"
            "lulesh.h"
            "lulesh-comm.cc"
            "lulesh-init.cc"
            "lulesh-util.cc"
            "lulesh-viz.cc"
            )

    target_compile_options("${test}"
            PRIVATE
            "-Wall"
            "-Wextra"
            )

    target_link_libraries ("${test}"
            PRIVATE "laik"
            PRIVATE "m"
            PRIVATE "mpi"
            )
endforeach ()
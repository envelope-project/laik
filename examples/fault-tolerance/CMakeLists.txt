# Build the C examples
foreach (test
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

add_subdirectory("osu")
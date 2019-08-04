# Build the C examples
foreach (test
        "checkpoint"
        "checkpoint-jac2d"
        "checkpoint-jac2d-recovery"
        "checkpoint_partitioner"
        "checkpoint-mandelbrot-recovery"
        )

    add_executable ("${test}"
            "${test}.c"
            )

    target_link_libraries ("${test}"
            PRIVATE "laik"
            PRIVATE "m"
            )
endforeach ()
# Build the C examples
foreach (test
        "checkpoint"
        "checkpoint-jac2d"
        "checkpoint-jac2d-recovery"
        "checkpoint_partitioner"
        )

    add_executable ("${test}"
            "${test}.c"
            )

    target_link_libraries ("${test}"
            PRIVATE "ssl"
            PRIVATE "crypto"
            PRIVATE "laik"
            PRIVATE "m"
            )
endforeach ()
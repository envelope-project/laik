# Build the C examples
foreach (test
        "checkpoint"
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
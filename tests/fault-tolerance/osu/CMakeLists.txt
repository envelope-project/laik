# Build the C examples
foreach (test
        "checkpoint-osu-latency"
        )

    add_executable ("${test}"
            "${test}.c"
            )

    target_link_libraries ("${test}"
            PRIVATE "laik"
            PRIVATE "m"
            )
endforeach ()
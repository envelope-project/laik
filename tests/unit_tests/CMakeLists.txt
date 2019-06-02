foreach (unit_test
        "core"
)
    add_executable("unit_test_${unit_test}" "${CMAKE_CURRENT_SOURCE_DIR}/${unit_test}.c")

    target_link_libraries ("unit_test_${unit_test}"
            PRIVATE "laik"
            PRIVATE "m"
            )

    add_test ("unit_test/${unit_test}" "${CMAKE_CURRENT_SOURCE_DIR}/${unit_test}")
endforeach ()

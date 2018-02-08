# Build the C examples
foreach (example
    "jac1d"
    "jac2d"
    "jac2d-ser"
    "jac3d"
    "markov"
    "markov2"
    "markov-ser"
    "propagation1d"
    "propagation2d"
    "spmv"
    "spmv2"
    "vsum"
    "vsum2"
    "vsum3"
)
    add_executable ("${example}"
        "${example}.c"
    )

    target_link_libraries ("${example}"
        PRIVATE "laik"
        PRIVATE "m"
    )
endforeach (example)

# If C++ support is enabled, build the C++ examples
if (enable-cpp-examples)
    foreach (example
        "raytracer"
    )
        add_executable ("${example}"
            "c++/${example}.cpp"
        )

        target_link_libraries ("${example}"
            PRIVATE "laik"
        )
    endforeach (example)
endif (enable-cpp-examples)

# If OpenMP support is enabled, pass its flags to the examples supporting it.
if (enable-openmp-examples)
    find_package (OpenMP REQUIRED)

    foreach (example
        "spmv2"
    )
        target_compile_options ("${example}"
            PRIVATE "${OpenMP_C_FLAGS}"
        )

        target_link_libraries ("${example}"
            PRIVATE "${OpenMP_C_FLAGS}"
        )
    endforeach (example)
endif (enable-openmp-examples)

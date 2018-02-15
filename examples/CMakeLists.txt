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
endforeach ()

# If C++ support is enabled, build the C++ examples
if (cpp-examples)
    foreach (example
        "raytracer"
    )
        add_executable ("${example}"
            "c++/${example}.cpp"
        )

        target_link_libraries ("${example}"
            PRIVATE "laik"
        )
    endforeach ()
endif ()

# If OpenMP support is enabled, pass its flags to the examples supporting it.
if (openmp-examples)
    find_package (OpenMP)

    if (OPENMP_FOUND)
        foreach (example
            "spmv2"
        )
            target_compile_options ("${example}"
                PRIVATE "${OpenMP_C_FLAGS}"
            )

            target_link_libraries ("${example}"
                PRIVATE "${OpenMP_C_FLAGS}"
            )
        endforeach ()
    elseif (skip-missing)
        message (WARNING "Dependency check for option 'openmp-examples' failed, skipping!")
    else ()
        message (FATAL_ERROR "Dependency check for option 'openmp-examples' failed, stopping!")
    endif ()
endif ()

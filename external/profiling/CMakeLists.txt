if (profiling-agent)
    find_pkgconfig ("papi")

    if (TARGET "papi")
        add_library (profileagent SHARED
            "profilagent.c"
        )

        target_include_directories (profileagent
            PUBLIC "."
        )

        target_link_libraries (profileagent
            PRIVATE "laik"
            PRIVATE "papi"
        )
    elseif (skip-missing)
        message (STATUS "Dependency check for option 'profiling-agent' failed, skipping!")
    else ()
        message (FATAL_ERROR "Dependency check for option 'profiling-agent' failed, stopping!")
    endif ()
endif ()

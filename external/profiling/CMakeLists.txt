if (enable-profiling-agent)
    add_pkgconfig_library ("papi")

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
endif (enable-profiling-agent)

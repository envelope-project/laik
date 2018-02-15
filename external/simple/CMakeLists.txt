if (simple-agent)
    add_library (simpleagent SHARED
        "simple-agent.c"
    )

    target_include_directories (simpleagent
        PUBLIC "."
    )

    target_link_libraries (simpleagent
        PRIVATE "laik"
    )
endif ()

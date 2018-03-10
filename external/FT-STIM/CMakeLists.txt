if (failure-simulator)
    find_pkgconfig ("libprotobuf-c")
    find_program (protoc "protoc-c")

    if (TARGET "libprotobuf-c" AND protoc)
        compile_protobuf_module ("laik_ext")

        add_library ("laikfti" SHARED
            "laik-ext-fti.c"
            "laik_ext.pb-c.c"
        )

        target_include_directories ("laikfti"
            PUBLIC "."
        )

        target_link_libraries ("laikfti"
            PRIVATE "libprotobuf-c"
        )
    elseif (skip-missing)
        message (STATUS "Dependency check for option 'failure-simulator' failed, skipping!")
    else ()
        message (FATAL_ERROR "Dependency check for option 'failure-simulator' failed, stopping!")
    endif ()
endif ()

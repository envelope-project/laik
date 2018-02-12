if (failure_simulator)
    add_pkgconfig_library ("libprotobuf-c")

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
endif ()

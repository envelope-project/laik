if enable_protobuf
    liblaikfti = declare_dependency (
        include_directories : include_directories ('.'),
        link_with : library (
            'laikfti',
            'laik-ext-fti.c',
            generator_protoc.process ('laik_ext.proto'),
            c_args : ['-Wno-unused-variable', '-Wno-format'],
            dependencies : [libprotobuf],
        ),
    )
endif

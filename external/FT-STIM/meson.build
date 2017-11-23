if enable_protobuf
    liblaikfti = declare_dependency (
        include_directories : include_directories ('.'),
        link_with : library (
            'laikfti',
            'laik-ext-fti.c',
            dependencies : [libprotobuf],
        ),
    )

    # FIXME: Simply use @CURRENT_SOURCE_DIR@ once meson >= 0.41 is available
    proto_path = run_command ('realpath', '--relative-to', meson.build_root(), '--', meson.current_source_dir()).stdout().split('\n')[0]

    custom_target (
        'laik_ext.pb-c',
        command : [program_protoc, '--proto_path', proto_path, '--c_out=@OUTDIR@', '@INPUT@'],
        input : ['laik_ext.proto'],
        output : ['@BASENAME@pb-c.c', '@BASENAME@.pb-c.h'],
        build_by_default : true,
    )
endif

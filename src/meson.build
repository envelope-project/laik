liblaik = declare_dependency (
    include_directories : include_directories('../include'),
    link_with: library (
        'laik',
        'backend.c',
        'backend-mpi.c',
        'backend-single.c',
        'core.c',
        'data.c',
        'debug.c',
        'external.c',
        'partitioner.c',
        'profiling.c',
        'program.c',
        'space.c',
        include_directories : include_directories('../include'),
        dependencies : [libdl, libmpi],
        install : true,
    ),
)

install_headers (
    '../include/laik-backend-mpi.h',
    '../include/laik-backend-single.h',
    '../include/laik.h',
    '../include/laik-internal.h',
)

install_headers (
    '../include/interface/agent.h',
    subdir : 'interface',
)

install_headers (
    '../include/laik/backend.h',
    '../include/laik/core.h',
    '../include/laik/core-internal.h',
    '../include/laik/data.h',
    '../include/laik/data-internal.h',
    '../include/laik/debug.h',
    '../include/laik/definitions.h',
    '../include/laik/ext.h',
    '../include/laik/profiling.h',
    '../include/laik/profiling-internal.h',
    '../include/laik/program.h',
    '../include/laik/program-internal.h',
    '../include/laik/space.h',
    '../include/laik/space-internal.h',
    subdir : 'laik',
)

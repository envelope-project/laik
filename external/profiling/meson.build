if enable_papi
    libprofileagent = declare_dependency (
        include_directories : include_directories ('.'),
        link_with : library (
            'profileagent',
            'profilagent.c',
            dependencies : [liblaik, libpapi],
        ),
    )
endif

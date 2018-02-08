# Base library
add_library ("laik" SHARED
    "backend.c"
    "core.c"
    "data.c"
    "debug.c"
    "external.c"
    "partitioner.c"
    "partitioning.c"
    "profiling.c"
    "program.c"
    "space.c"
    "type.c"
)

target_include_directories ("laik"
    PUBLIC "../include"
)

target_link_libraries ("laik"
    PRIVATE "${CMAKE_DL_LIBS}"
)

# Optional MPI backend
if (enable-mpi-backend)
    add_pkgconfig_library ("mpi")

    target_sources ("laik"
        PRIVATE "backend-mpi.c"
    )

    target_compile_definitions ("laik"
        PRIVATE "USE_MPI"
    )

    target_link_libraries ("laik"
        PRIVATE "mpi"
    )
endif (enable-mpi-backend)

# Optional Single backend
if (enable-single-backend)
    target_sources ("laik"
        PRIVATE "backend-single.c"
    )
endif (enable-single-backend)

# Installation rules
install (TARGETS "laik" DESTINATION "lib")

install (FILES "../include/laik.h"                DESTINATION "include")
install (FILES "../include/laik-backend-mpi.h"    DESTINATION "include")
install (FILES "../include/laik-backend-single.h" DESTINATION "include")
install (FILES "../include/interface/agent.h"     DESTINATION "include/interface")
install (FILES "../include/laik/backend.h"        DESTINATION "include/laik")
install (FILES "../include/laik/core.h"           DESTINATION "include/laik")
install (FILES "../include/laik/data.h"           DESTINATION "include/laik")
install (FILES "../include/laik/debug.h"          DESTINATION "include/laik")
install (FILES "../include/laik/definitions.h"    DESTINATION "include/laik")
install (FILES "../include/laik/ext.h"            DESTINATION "include/laik")
install (FILES "../include/laik/profiling.h"      DESTINATION "include/laik")
install (FILES "../include/laik/program.h"        DESTINATION "include/laik")
install (FILES "../include/laik/space.h"          DESTINATION "include/laik")

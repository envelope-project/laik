# Build the C examples
FILE(GLOB SRCS *.cc)
FILE(GLOB HEADERS *.h)

add_executable ("lulesh"
        ${SRCS}
        ${HEADERS}
        "../util/fault-tolerance-options.h"
        "../util/fault-tolerance-options.c"
        )

target_compile_options("lulesh"
        PRIVATE
        "-Wall"
        "-Wextra"
        )

add_definitions("-DUSE_MPI=1" "-DREPARTITIONING" "-DFAULT_TOLERANCE")

foreach(SOURCE ${SRCS} ${HEADERS})
    set_source_files_properties( ${SOURCE} PROPERTIES COMPILE_FLAGS "-Wno-error=implicit-fallthrough -Wno-error=unused-parameter -Wno-error=unused-but-set-parameter -Wno-error=unknown-pragmas" )
endforeach()

target_link_libraries ("lulesh"
        PRIVATE "laik"
        PRIVATE "m"
        PRIVATE "mpi"
        )


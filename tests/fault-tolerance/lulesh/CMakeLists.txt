# Build the C examples
FILE(GLOB SRCS *.cc)
FILE(GLOB HEADERS *.h)

add_executable ("lulesh"
        ${SRCS}
        ${HEADERS}
        )

target_compile_options("lulesh"
        PRIVATE
        "-Wall"
        "-Wextra"
        )

target_link_libraries ("lulesh"
        PRIVATE "laik"
        PRIVATE "m"
        PRIVATE "mpi"
        )


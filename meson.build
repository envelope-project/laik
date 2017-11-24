# Main project declaration
project (
    'laik',
    ['c'],
    default_options : [
        'buildtype=debug',
        'c_std=gnu11',
        'cpp_std=gnu++14',
        'warning_level=1',
        'werror=true',
    ],
    license : 'LGPLv3+',
    meson_version : '>= 0.40',
    version : '0.1',
)

# Required C compiler
compiler_c = meson.get_compiler ('c')

# Required C libraries
libdl   = compiler_c.find_library ('libdl')
libm    = compiler_c.find_library ('libm')
libgomp = compiler_c.find_library ('libgomp')

# Required UNIX programs
program_cmp  = find_program ('cmp')
program_env  = find_program ('env')
program_sort = find_program ('sort')

# Optional C++ compiler
enable_cpp = get_option ('enable-cpp')
if enable_cpp
    add_languages ('cpp')
endif

# Optional documenation
enable_doc = get_option ('enable-doc')
if enable_doc
    program_pandoc = find_program ('pandoc')
endif

# Optional Mosquitto support
enable_mosquitto = get_option ('enable-mosquitto')
if enable_mosquitto
    libmosquitto =  compiler_c.find_library ('mosquitto')

    add_project_arguments ('-DUSE_MQTT', language : 'c')
endif

# Optional MPI support
enable_mpi = get_option ('enable-mpi')
if enable_mpi
    libmpi = dependency ('mpi')

    program_mpirun = find_program ('mpirun')

    add_project_arguments ('-DUSE_MPI', language : 'c')
else
    libmpi = []
endif

# Optional PAPI support
enable_papi = get_option ('enable-papi')
if enable_papi
    libpapi = compiler_c.find_library ('papi')
endif

# Optional protobuf support
enable_protobuf = get_option ('enable-protobuf')
if enable_protobuf
    libprotobuf = compiler_c.find_library ('protobuf-c')

    program_protoc = find_program ('protoc-c')

    generator_protoc = generator (
        program_protoc,
        arguments : ['--proto_path=@CURRENT_SOURCE_DIR@', '--c_out=@BUILD_DIR@', '@INPUT@'],
        output : ['@BASENAME@.pb-c.c', '@BASENAME@.pb-c.h'],
    )
endif

# Build the main library
subdir ('src')

# Build the examples
subdir ('examples')

# Build the external libraries
subdir ('external/FT-STIM')
subdir ('external/MQTT')
subdir ('external/profiling')
subdir ('external/simple')

# Build the documentation
subdir ('doc')

# Run the tests
subdir ('tests')
subdir ('tests/mpi')

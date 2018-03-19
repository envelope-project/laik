# Start with a configurable base image
ARG IMG="debian"
FROM "${IMG}"

# Declare the arguments
ARG PKG="gcc g++"
ARG CC="gcc"
ARG CXX="g++"

# Update the package lists
RUN apt-get update

# Install the packages needed for the build
RUN env DEBIAN_FRONTEND=noninteractive apt-get install --yes \
    "bear" \
    "clang" \
    "clang-tidy" \
    "libglib2.0-dev" \
    "libmosquitto-dev" \
    "libopenmpi-dev" \
    "libpapi-dev" \
    "libprotobuf-c-dev" \
    "make" \
    "openmpi-bin" \
    "openssh-client" \
    "pkg-config" \
    "protobuf-c-compiler" \
    "python" \
    ${PKG}

# Copy the current directory to the container and continue inside it
COPY "." "/mnt"
WORKDIR "/mnt"

# mpirun doesn't like being run as root, so continue as an unpriviledged user
RUN useradd "user"
RUN chown --recursive "user:user" "."
USER "user"

# Build and test
RUN CC="${CC}" CXX="${CXX}" ./configure
RUN OMPI_CC="${CC}" bear make
RUN make test
RUN find 'src' -name '*.c' -print0 | xargs --null --max-args='1' clang-tidy -p 'compile_commands.json'

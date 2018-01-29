# Start with a configurable base image
ARG IMG="debian"
FROM "${IMG}"

# Declare the arguments
ARG PKG="gcc g++"
ARG CC="gcc"
ARG CXX="g++"

# Set up LC_ALL so the sort order is as expected in the tests
ENV LC_ALL="en_US.UTF8"

# Update the package lists
RUN apt-get update

# Install the packages needed for the build
RUN env DEBIAN_FRONTEND=noninteractive apt-get install --yes \
    "libmosquitto-dev" \
    "libopenmpi-dev" \
    "libpapi-dev" \
    "libprotobuf-c-dev" \
    "locales-all" \
    "make" \
    "openmpi-bin" \
    "openssh-client" \
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
RUN OMPI_CC="${CC}" make
RUN make test

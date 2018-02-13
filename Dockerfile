# Start with a configurable base image
ARG IMG="debian"
FROM "${IMG}"

# Declare the arguments
ARG PKG="gcc g++"

# Update the package lists
RUN apt-get update

# Install the packages needed for the build
RUN env DEBIAN_FRONTEND=noninteractive apt-get install --yes \
    "cmake" \
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

# mpirun doesn't like being run as root, add an unpriviledged user
RUN useradd "user"
USER "user"

# start in /mnt
WORKDIR "/mnt"

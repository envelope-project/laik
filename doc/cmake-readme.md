# Building with CMake

If you want to build the project, the first step is to create a dedicated build
directory. In theory, this directory could be located anywhere on your file
system, but we'll simply use the ```build``` subdirectory here:

    mkdir build

Now, change into your build directory:

    cd build

Next, you need to configure your build, for example like this:

    cmake -Denable-mpi-backend=off ..

This tells CMake that the source files are located in the parent directory and
that it should create the build files with MPI support disabled. You can of
course use different options here, or none at all if you just want the default.
Have a look at the main ```CMakeLists.txt``` file for all supported options!

Now you can build the project:

    make

Ok, great! Now let's try re-configuring the build:

    cmake -Denable-mpi-backend=on ..

This enabled MPI support. Now let's rebuild:

    make

To run the tests and get a nice overview of the results, you can use CMake's
test support:

    ctest

Please note that after the initial setup, you'll never have to call ```cmake```
again (unless you want to change the configuration options again): The generated
build files are smart enough to ask CMake for any new rules from the
```CMakeLists.txt``` file before starting the build process!

Finally, installing works just like with native ```make```:

    make install

# Building with meson

LAIK optionally supports using ```meson``` for building. If you have a C
compiler and ```meson``` installed, simply calling

    meson setup build

will configure LAIK with the minimal feature set in the newly-created
```build``` directory. If you want to enable optional features, you could for
example modify the command like this:

    meson setup -Denable-mpi=true build-mpi

If you have the necessary packages (see ```README.md```) installed, this will
configure LAIK with MPI enabled in ```build-mpi```. However please note that you
don't have to create a new directory everytime you want to change the options,
but can also modify the options of an existing directory like this:

    meson configure build -Denable-mpi=true

Have a look at the ```meson-options.txt``` file and the output of the ```meson
configure ${YOUR_BUILD_DIRECTORY}``` command to learn about available options
and their current values! In order to actually build LAIK with the specified
configuration, all you have to do is call ```ninja```, the actual build tool
used by ```meson```:

    ninja -C build

However, please note that after the initial setup, you'll never have to call
```meson``` again (unless you want to change the configuration options again):
```ninja``` is smart enough to ask meson for any new rules from the
```meson.build``` file before starting the build process!

Finally, installing/uninstalling works just like with ```make```:

    ninja -C build install
    ninja -C build uninstall

## Building with OpenMP support

If you want to build with support for OpenMP (the configure script will enable
this automatically if your compiler supports it), you'll need the corresponding
runtime libraries installed. Most of the time, these should be shipped along
with your compiler, but there are some setups known to be broken:

  1. Debian/Ubuntu's ```clang``` package [does not depend on the corresponding
     OpenMP runtime](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=882781),
     so you'll have to install ```libomp-dev``` manually.

  2. Travis' ```clang``` integration includes the development headers for the
     OpenMP runtime, [but not the actual shared
     library](https://github.com/travis-ci/travis-ci/issues/8315#issuecomment-328672030).
     Since Travis' "current" OS is still Ubuntu Trusty (which doesn't ship
     ```libomp-dev```), you'll have to work around this bug like this:

     ```
     # Install the old Intel OpenMP runtime, which is now LLVM's libomp
     sudo apt-get install -y libiomp5

     # Create a compatiblity symlink
     sudo ln --symbolic /usr/lib/libiomp5.so.5 /usr/lib/libomp.so
     ```

  3. Intel's ICC compiler also needs the ```libomp-dev``` package installed.
     Whether that happens automatically may depend on your installation method.

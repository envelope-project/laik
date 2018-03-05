# Managing your includes with IWYU

IWYU is an invaluable tool to keep your includes to a minimum and trace **why**
you are including **what**.

## Installation

IWYU is probably packaged for your distribution, look for a package called
```iwyu``` or ```include-what-you-use```. Otherwise, you can of course also
compile it from source from
[upstream](https://github.com/include-what-you-use/include-what-you-use/).

Please note that IWYU needs clang (it is essentially just a wrapper around
clang), and more specifically it needs the same version of clang during build
and run time. Some distributions (e.g.
[Debian](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=722132)) get this
wrong, so you might want to check for yourself:

    $ strings /usr/bin/include-what-you-use | grep /usr/lib/llvm | head -1
    /usr/lib/llvm-5.0/include/llvm/Support/MathExtras.h
    $ # Ok, we need clang 5.0
    $ clang-5.0 --version
    clang version 5.0.1-2 (tags/RELEASE_501/final)
    Target: x86_64-pc-linux-gnu
    Thread model: posix
    InstalledDir: /usr/bin
    $ # Everything good
    $ 

## How to use it

    $ tools/iwyu/iwyu.sh src/backend.c

    src/backend.c should add these lines:
    #include <laik.h>     // for Laik_Transition, TaskGroup, _Laik_Transition
    #include <stdbool.h>  // for true, bool, false

    src/backend.c should remove these lines:
    - #include <stdlib.h>  // lines 9-9
    - #include "laik-internal.h"  // lines 6-6

    The full include-list for src/backend.c:
    #include <assert.h>   // for assert
    #include <laik.h>     // for Laik_Transition, TaskGroup, _Laik_Transition
    #include <stdbool.h>  // for true, bool, false
    ---
    $

Checking files which require special compiler options is also possible:

    $ tools/iwyu/iwyu.sh src/backend-mpi.c

    (src/backend-mpi.c has correct #includes/fwd-decls)
    $ # Ah, I forgot the USE_MPI guard
    $ tools/iwyu/iwyu.sh -D USE_MPI src/backend-mpi.c

    src/backend-mpi.c should add these lines:
    #include <laik-internal.h>  // for redTOp, _Laik_Group, TaskGroup, _Laik_Map...
    #include <laik.h>           // for Laik_Group, Laik_Mapping, laik_log, Laik_...
    struct _MPIData;
    struct _MPIGroupData;

    src/backend-mpi.c should remove these lines:
    - #include <string.h>  // lines 19-19
    - #include <sys/types.h>  // lines 17-17
    - #include "laik-backend-mpi.h"  // lines 9-9
    - #include "laik-internal.h"  // lines 8-8

    The full include-list for src/backend-mpi.c:
    #include <assert.h>         // for assert
    #include <laik-internal.h>  // for redTOp, _Laik_Group, TaskGroup, _Laik_Map...
    #include <laik.h>           // for Laik_Group, Laik_Mapping, laik_log, Laik_...
    #include <mpi.h>            // for MPI_Recv, MPI_Send, MPI_Allreduce, MPI_Re...
    #include <stdbool.h>        // for bool, true, false
    #include <stdint.h>         // for int64_t, uint64_t
    #include <stdio.h>          // for sprintf
    #include <stdlib.h>         // for exit, malloc, atoi, getenv
    #include <unistd.h>         // for getpid, usleep
    struct _MPIData;
    struct _MPIGroupData;
    ---
    $

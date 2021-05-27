#!/bin/sh
timeout 2 ./tcp2run -n 2 -s 2 ../../examples/resize 1 | LC_ALL='C' sort > test-resize-2-2.out
./mycmp test-resize-2-2.out "$(dirname -- "${0}")/test-resize-2-2.expected"

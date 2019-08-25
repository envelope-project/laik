#!/usr/bin/env bash

cd lib/ulfm2/
perl autogen.pl
./configure
make
cd ../..

cd lib/glib
meson _build
ninja -C _build
cd ../..

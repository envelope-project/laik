#!/bin/bash

if [ $# -ne 1 ]
then
  echo "Need to pass which tool to run"
  exit 255
fi

case "$1" in
  "sync-up")
    rsync -av -e "ssh bodev@lxhalle.in.tum.de ssh" --exclude ".*" --exclude "cmake-build-*" --exclude="out" --exclude "lib" --exclude "laik_experiments/data" ./ ga26poh3@skx.supermuc.lrz.de:/dss/dsshome1/08/ga26poh3/laik
  ;;
  "sync-down-data")
    rsync -av -e "ssh bodev@lxhalle.in.tum.de ssh" ga26poh3@skx.supermuc.lrz.de:/dss/dsshome1/08/ga26poh3/laik/laik_experiments/data ./laik_experiments/
  ;;
  "sh")
    echo "Not implemented"
  ;;
esac
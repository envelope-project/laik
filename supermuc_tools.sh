#!/bin/bash

if [ $# -lt 1 ]
then
  echo "Need to pass which tool to run"
  exit 255
fi

JUMP_HOST=bodev@lxhalle.in.tum.de

case "$1" in
  "sync-up")
    rsync -av -e "ssh -J $JUMP_HOST" --exclude ".*" --exclude "cmake-build-*" --exclude="out" --exclude "lib" --exclude "laik_experiments/data" ./ ga26poh3@skx.supermuc.lrz.de:/dss/dsshome1/08/ga26poh3/laik
  ;;
  "sync-down-data")
    rsync -av -e "ssh -J $JUMP_HOST" ga26poh3@skx.supermuc.lrz.de:/dss/dsshome1/08/ga26poh3/laik/laik_experiments/data ./laik_experiments/
  ;;
  "sh")
    ssh -J $JUMP_HOST ga26poh3@skx.supermuc.lrz.de
  ;;
  "maimuc-sync-up")
    rsync -av -e "ssh -J bodev@himmuc.caps.in.tum.de" --exclude ".*" --exclude "cmake-build-*" --exclude="out" --exclude "lib" --exclude "laik_experiments/data" ./ login@maimuc.caps.in.tum.de:/home/pi/ga26poh/laik/
  ;;
  "maimuc-sync-down-data")
#    rsync -av -e "ssh -J bodev@himmuc.caps.in.tum.de" ga26poh3@skx.supermuc.lrz.de:/dss/dsshome1/08/ga26poh3/laik/laik_experiments/data ./laik_experiments/
  ;;
  "himmuc-sync-up")
    rsync -av --exclude ".*" --exclude "cmake-build-*" --exclude="out" --exclude "lib" --exclude "laik_experiments/data" ./ bodev@himmuc.caps.in.tum.de:/u/home/bodev/laik/
  ;;
  "himmuc-sync-down-data")
    rsync -av bodev@himmuc.caps.in.tum.de:/u/home/bodev/laik/laik_experiments/data ./laik_experiments/
  ;;
  "himmuc-sh")
    ssh bodev@himmuc.caps.in.tum.de
  ;;
  "maimuc-sh")
    ssh -J bodev@himmuc.caps.in.tum.de login@maimuc.caps.in.tum.de
  ;;
  "maimuc-live")
    while true
    do
      rsync -av -e "ssh -J bodev@himmuc.caps.in.tum.de login@maimuc.caps.in.tum.de ssh " pi@mai00:~/ga26poh/laik/output/data_live_0_0.ppm ./output/
#      rsync -av pi@mai00:~/ga26poh/laik/output/data_live_0_0.ppm ./output/
      sleep 1
    done
  ;;
  "maimuc-remote-vis")
    xopen "$(which ssh)" -v -C -Y -J bodev@himmuc.caps.in.tum.de login@maimuc.caps.in.tum.de bash -c \'DISPLAY=:0 python3 /home/pi/ga26poh/laik/laik_experiments/visualizer.py sleep 10000\'
  ;;
  "maimuc-sync-data")
    for i in 0 1 3 4 5 6 7 8 9
    do
      echo "Sync to $i"
      rsync -avr --info=progress2 --delete ~/ga26poh mai0$i:~/
    done
    ;;
  "maimuc-sync-python")
    for i in 0 1 3 4 5 6 7 8 9
    do
      echo "Sync to $i"
      rsync -a --info=progress2 --delete ~/.local mai0$i:~/
    done
    ;;
  "maimuc-all-other")
    for i in 0 1 3 4 5 6 7 8 9
    do
      echo "Run on $i: $2"
      ssh mai0$i "$2"
    done
    ;;
  "maimuc-ssh-apt-all")
    for i in 0 1 3 4 5 6 7 8 9
    do
      echo "Run on $i: $2"
      ssh-apt mai0$i "$2"
    done
    ;;
  "maimuc-compile-and-sync")
    cd cmake-build-debug_wsl || exit
    rm -r CMakeCache.txt CMakeFiles/ cmake_install.cmake
    CC=$(which gcc)
    CXX=$(which g++)
    export CC
    export CXX
    cmake -DCMAKE_SYSTEM_PREFIX_PATH=~/ga26poh/lib/ulfm/ -DMPI_C_COMPILER=~/ga26poh/lib/ulfm/bin/mpicc -DMPI_CXX_COMPILER=~/ga26poh/lib/ulfm/bin/mpicxx ../ || exit
    make checkpoint-jac2d-recovery || exit
    cd .. || exit
    ./supermuc_tools.sh maimuc-sync-data || exit
    ;;
  *)
    echo "Unrecognized: $1"
    ;;
esac

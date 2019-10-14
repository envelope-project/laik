#!/bin/bash

if [ $# -lt 1 ]
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
    ssh -o ProxyCommand="ssh -W %h:%p bodev@lxhalle.in.tum.de"  ga26poh3@skx.supermuc.lrz.de
  ;;
  "maimuc-sync-up")
    rsync -av -e "ssh -J bodev@himmuc.caps.in.tum.de" --exclude ".*" --exclude "cmake-build-*" --exclude="out" --exclude "lib" --exclude "laik_experiments/data" ./ login@maimuc.caps.in.tum.de:/home/pi/ga26poh/laik/
  ;;
  "maimuc-sync-down-data")
    rsync -av -e "ssh -J bodev@himmuc.caps.in.tum.de" ga26poh3@skx.supermuc.lrz.de:/dss/dsshome1/08/ga26poh3/laik/laik_experiments/data ./laik_experiments/
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
    make checkpoint-jac2d-recovery
    cd .. || exit
    ./supermuc_tools.sh maimuc-sync-data
    ;;
  *)
    echo "Unrecognized: $1"
    ;;
esac
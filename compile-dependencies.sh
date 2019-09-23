#!/bin/bash
# Job Name and Files (also --job-name)
#SBATCH -J compile-dependencies
#Output and error (also --output, --error):
#SBATCH -o ./%x.%j.out
#SBATCH -e ./%x.%j.err
#Initial working directory (also --chdir):
#SBATCH -D ./
#Notification and type
#SBATCH --mail-type=END
#SBATCH --mail-user=vincent.bode@tum.de
# Wall clock limit:
#SBATCH --time=01:00:00
#SBATCH --no-requeue
#Setup of execution environment
#SBATCH --export=NONE
#SBATCH --get-user-env
#SBATCH --account=pn72xo
#Number of nodes and MPI tasks per node:
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --ear=off
#SBATCH --partition=general
#constraints are optional
#--constraint="scratch&work"

#Important
module load slurm_setup

module load autoconf
module load automake

cd lib/icldistcomp-ulfm2-2e75c73cc620/ || exit
perl autogen.pl
./configure
make
cd ../..

cd lib/glib || exit
meson _build
ninja -C _build
cd ../..

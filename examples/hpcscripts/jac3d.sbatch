#!/bin/bash

#SBATCH -o /home/hpc/pr63qi/ga39pid2/outfile/jac3d.%j.out
#SBATCH -D /home/hpc/pr63qi/ga39pid2/
#SBATCH -J laik-jac3d
#SBATCH --get-user-env
#SBATCH --cluster=mpp2
#SBATCH --ntasks=560
#SBATCH --cpus-per-task=1
#SBATCH --mail-type=end
#SBATCH --mail-user=d.yang@tum.de
#SBATCH --export=NONE
#SBATCH --time=00:30:00

source /etc/profile.d/modules.sh
mpiexec -genv OMP_NUM_THREADS 1 -genv I_MPI_PIN_DOMAIN numa -genv I_MPI_PIN=1 -genv I_MPI_PIN_MODE=lib -n 560 ~/laik/examples/jac3d -p 2000 200

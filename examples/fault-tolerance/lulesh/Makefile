#default build suggestion of MPI + OPENMP with gcc on Livermore machines you might have to change the compiler name

# defaults
LAIK_ROOT = laik
OPT = -O0 -g

# local settings, may overwrite defaults
-include Makefile.local

SHELL = /bin/sh
.SUFFIXES: .cc .o

LULESH_EXEC = lulesh2.0

MPI_INC = /opt/local/include/openmpi
MPI_LIB = /opt/local/lib

LAIK_INC =-I$(LAIK_ROOT)/include/
LAIK_LIB =-L$(LAIK_ROOT)/ -llaik

SERCXX = g++ -DUSE_MPI=0
MPICXX = mpic++ 
CXX = $(MPICXX)

SOURCES2.0 = \
	lulesh.cc \
	lulesh-comm.cc \
	lulesh-viz.cc \
	lulesh-util.cc \
	lulesh-init.cc \
	laik_partitioners.cc \
	laik_vector.cc \
	laik-lulesh-repartition.cc \
	laik_vector_comm_exclusive_halo.cc \
	laik_vector_comm_overlapping_overlapping.cc \
	laik_vector_repart_exclusive.cc \
	laik_vector_repart_overlapping.cc

OBJECTS2.0 = $(SOURCES2.0:.cc=.o)

TARGET = REPARTITIONING

#Default build suggestions with OpenMP for g++
#CXXFLAGS = -g $(OPT) -std=c++11 -fopenmp -I. -Wall $(LAIK_INC) -DUSE_MPI=1 -DREPARTITIONING=1
CXXFLAGS = $(OPT) -std=c++11 -fopenmp -I. -Wall $(LAIK_INC) -DUSE_MPI=1 -D$(TARGET)=1
LDFLAGS = $(OPT) -std=c++11 -fopenmp -Wl,-rpath,$(abspath $(LAIK_ROOT)) $(LAIK_LIB)  -lmpi

#Below are reasonable default flags for a serial build
#CXXFLAGS = -g -O3 -I. -Wall
#LDFLAGS = -g -O3 

#common places you might find silo on the Livermore machines.
#SILO_INCDIR = /opt/local/include
#SILO_LIBDIR = /opt/local/lib
#SILO_INCDIR = ./silo/4.9/1.8.10.1/include
#SILO_LIBDIR = ./silo/4.9/1.8.10.1/lib

#If you do not have silo and visit you can get them at:
#silo:  https://wci.llnl.gov/codes/silo/downloads.html
#visit: https://wci.llnl.gov/codes/visit/download.html

#below is and example of how to make with silo, hdf5 to get vizulization by default all this is turned off.  All paths are Livermore specific.
#CXXFLAGS = -g -DVIZ_MESH -I${SILO_INCDIR} -Wall -Wno-pragmas
#LDFLAGS = -g -L${SILO_LIBDIR} -Wl,-rpath -Wl,${SILO_LIBDIR} -lsiloh5 -lhdf5

.cc.o: lulesh.h
	@echo "Building $<"
	$(CXX) -c $(CXXFLAGS) -o $@  $<

all: $(LULESH_EXEC)

lulesh2.0: $(OBJECTS2.0)
	@echo "Linking"
	$(CXX) $(OBJECTS2.0) $(LDFLAGS) -lm -o $@

clean:
	/bin/rm -f *.o *~ $(OBJECTS) $(LULESH_EXEC)
	/bin/rm -rf *.dSYM

sync:
	rsync -a . sksmall:laik-lulesh/

tar: clean
	cd .. ; tar cvf lulesh-2.0.tar LULESH-2.0 ; mv lulesh-2.0.tar LULESH-2.0

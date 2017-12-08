# default settings
OPT = -g

# settings from 'configure', may overwrite defaults
-include ../../Makefile.config

EXAMPLES = raytracer

CXXFLAGS = $(OPT) $(DEFS) -I../../include
LAIKLIB = $(abspath ../../liblaik.so)

all: $(EXAMPLES)

raytracer: raytracer.cpp $(LAIKLIB)
	$(CXX) $(CXXFLAGS) raytracer.cpp $(LAIKLIB) -o raytracer

clean:
	rm -f *.o *~ *.ppm $(EXAMPLES)

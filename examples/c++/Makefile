# default settings
OPT = -g

# settings from 'configure', may overwrite defaults
-include ../../Makefile.config

EXAMPLES = raytracer

CXXFLAGS = $(OPT) $(WARN) $(DEFS) -I$(SDIR)../../include
LAIKLIB = ../../liblaik.so

all: $(EXAMPLES)

raytracer: $(SDIR)raytracer.cpp $(LAIKLIB)
	$(CXX) $(CXXFLAGS) $< $(LAIKLIB) -o $@

clean:
	rm -f *.o *~ *.ppm $(EXAMPLES)

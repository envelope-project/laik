include Makefile.common

CFLAGS += -I 'include'

PREFIX ?= /usr/local
OPT = -g
SUBDIRS=examples

SRCS = $(wildcard src/*.c)
HEADERS = $(wildcard include/*.h include/laik/*.h)
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

# instruct GCC to produce dependency files
CFLAGS+=-MMD -MP

MPICC ?= $(CC)
LAIKLIB = liblaik.so

# build targets
.PHONY: $(SUBDIRS)

all: $(LAIKLIB) $(SUBDIRS)

external/MQTT: $(LAIKLIB)
	cd external/MQTT && $(MAKE)

external/simple: $(LAIKLIB)
	cd external/simple && $(MAKE)

src/backend-mpi.o: src/backend-mpi.c
	$(MPICC) $(CFLAGS) -c -o src/backend-mpi.o src/backend-mpi.c

$(LAIKLIB): $(OBJS)
	$(MPICC) $(CFLAGS) -shared -o $(LAIKLIB) $(OBJS) -ldl

examples: $(LAIKLIB)
	cd examples && $(MAKE)



# tests
test: examples
	make -C tests

# tidy
tidy:
	make clean
	bear make test
	git ls-files '*.c' | xargs -P 1 -I{} clang-tidy {};

tidy-clean: clean
	rm -f compile_commands.json

# clean targets
SUBDIRS_CLEAN=$(addprefix clean_, $(SUBDIRS))
.PHONY: $(SUBDIRS_CLEAN)

clean: clean_laik $(SUBDIRS_CLEAN)

clean_laik:
	rm -f *~ *.o $(OBJS) $(DEPS) $(LAIKLIB)

$(SUBDIRS_CLEAN): clean_%:
	+$(MAKE) clean -C $*


# install targets
install: install_laik

install_laik: $(LAIKLIB) $(HEADERS)
	cp $(wildcard include/*.h) $(PREFIX)/include
	mkdir -p $(PREFIX)/include/laik
	cp $(wildcard include/laik/*.h) $(PREFIX)/include/laik
	mkdir -p $(PREFIX)/include/interface
	cp $(wildcard include/interface/*.h) $(PREFIX)/include/interface
	mkdir -p $(PREFIX)/lib
	cp $(LAIKLIB) $(PREFIX)/lib

# install targets
uninstall: uninstall_laik

uninstall_laik:
	rm -rf $(PREFIX)/include/laik
	rm -f $(PREFIX)/include/laik.h
	rm -f $(PREFIX)/include/laik-internal.h
	rm -f $(PREFIX)/include/laik-backend-*.h
	rm -f $(PREFIX)/lib/liblaik.*

# include previously generated dependency rules if existing
-include $(DEPS)

# default settings
PREFIX=/usr/local
OPT=-O0 -g
WARN=-Wall -Wextra
SUBDIRS=examples

# settings from 'configure', may overwrite defaults
-include Makefile.config

LDFLAGS=$(OPT)
IFLAGS=-I$(SDIR)include -I$(SDIR)src
LDLIBS=-ldl

SRCS = $(wildcard $(SDIR)src/*.c)
ifdef USE_TCP
SRCS += $(wildcard $(SDIR)src/backends/tcp/*.c)
IFLAGS += $(TCP_INC)
LDLIBS += $(TCP_LIBS)
endif
HEADERS = $(wildcard $(SDIR)include/*.h $(SDIR)include/laik/*.h)
OBJS = $(SRCS:$(SDIR)%.c=%.o) revinfo.o

CFLAGS=$(OPT) $(WARN) $(DEFS) $(IFLAGS) -std=gnu99 -fPIC

# instruct GCC to produce dependency files
DEPS = $(SRCS:$(SDIR)%.c=%.d)
CFLAGS+=-MMD -MP

# MPICC always must be set, even if MPI not found (then use regular C compiler)
MPICC ?= $(CC)
LAIKLIB = liblaik.so

GIT_VERSION := $(shell git describe --abbrev=5 --dirty --always --tags)

# build targets
.PHONY: $(SUBDIRS) force

all: $(LAIKLIB) $(SUBDIRS)

# version/compile information for first line of LAIK_LOG=2
# only trigger compile if git revision changes
git-version: force
	echo '$(GIT_VERSION)' | cmp -s - $@ || echo '$(GIT_VERSION)' > $@

revinfo.c: git-version
	@echo "// Warning: Generated, changes will be overwritten" > revinfo.c
	@echo "#include <laik.h>" >> revinfo.c
	@echo "void laik_log_append_info(void) { laik_log_append(" >> revinfo.c
	@echo "  \"LAIK git version: "$(GIT_VERSION)" \"" >> revinfo.c
	@echo "  \"OPT='"$(OPT)"'\"); }" >> revinfo.c


external/MQTT: $(LAIKLIB)
	cd external/MQTT && $(MAKE)

external/simple: $(LAIKLIB)
	cd external/simple && $(MAKE)

src/%.o: $(SDIR)src/%.c
	$(CC) -c $(CFLAGS) -c $< -o $@

src/backend-mpi.o: $(SDIR)src/backend-mpi.c
	$(MPICC) $(CFLAGS) -c -o src/backend-mpi.o $(SDIR)src/backend-mpi.c

$(LAIKLIB): $(OBJS)
	$(MPICC) $(CFLAGS) -shared -o $(abspath $(LAIKLIB)) $(OBJS) $(LDLIBS)

examples: $(LAIKLIB)
	cd examples && $(MAKE)

examples/c++: $(LAIKLIB)
	cd examples/c++ && $(MAKE)


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
	rm -f *~ *.o revinfo.c $(OBJS) $(DEPS) $(LAIKLIB)

$(SUBDIRS_CLEAN): clean_%:
	+$(MAKE) clean -C $*


# install targets
install: install_laik

install_laik: $(LAIKLIB) $(HEADERS)
	cp $(wildcard $(SDIR)include/*.h) $(PREFIX)/include
	mkdir -p $(PREFIX)/include/laik
	cp $(wildcard $(SDIR)include/laik/*.h) $(PREFIX)/include/laik
	#mkdir -p $(PREFIX)/include/interface
	#cp $(wildcard $(SDIR)include/interface/*.h) $(PREFIX)/include/interface
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

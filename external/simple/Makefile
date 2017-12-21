# Makefile Recreated by Dai Yang
#
#	MQTT Client using Mosquitto for LAIK
# Dependencies: libprotobuf-c, libmosquitto
#
# (C) 2017 LRR, Technische Universitaet Muenchen
#

CC= cc
OPT=-g

# pull in global config: CC, OPT, DEFS
-include ../../Makefile.config

CFLAGS = -std=gnu99 -I$(SDIR)../../include -fPIC -Wall -Wextra
CFLAGS += $(OPT) $(DEFS) -MMD -MP

LDFLAGS= -shared
DEBUGFLAGS= -O0 -D DEBUG
RELEASEFLAGS= -O3 -D NDEBUG -combine -fwhole-program


TARGET  = libsimpleagent.so
SOURCES = $(wildcard $(SDIR)*.c)
HEADERS = $(wildcard $(SDIR)*.h)
OBJECTS = $(SOURCES:$(SDIR)%.c=%.o)

PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin

all:  $(TARGET)

%.o: $(SDIR)%.c
	$(CC) -c $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(DEBUGFLAGS) $(INC) -o $(TARGET) $(OBJECTS)

clean:
	rm -f *.o
	rm -f *.d
	rm -f libsimpleagent.so

.PHONY: all clean

# Dependencies tracking:
-include *.d

# Makefile Recreated by Dai Yang
#
#	MQTT Client using Mosquitto for LAIK
# Dependencies: libprotobuf-c, libmosquitto
#
# (C) 2017 LRR, Technische Universitaet Muenchen
#

CC=cc
OPT=-g

# pull in global config: CC, OPT, DEFS
-include ../../Makefile.config

CFLAGS = -std=gnu99 -I$(SDIR)../../include -fPIC -Wall -Wextra
CFLAGS += $(OPT) $(DEFS) -MMD -MP

LDFLAGS= -shared -lyaml
DEBUGFLAGS= -O0 -D DEBUG
RELEASEFLAGS= -O3 -D NDEBUG -combine -fwhole-program

TARGET  = libfileagent.so
SOURCES =  $(SDIR)fileagent.c
HEADERS = $(wildcard *.h)
OBJECTS = $(SOURCES:$(SDIR)%.c=%.o)

PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin

%.o: $(SDIR)%.c
	$(CC) -c $(CFLAGS) -c $< -o $@

$(TARGET):  $(OBJECTS)
	$(CC) $(FLAGS) $(CFLAGS) $(DEBUGFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJECTS) 

clean:
	rm -f *.o
	rm -f *.d
	rm -f *.pb-c.*
	rm -f *.so

.PHONY: all clean

# Dependencies tracking:
-include *.d

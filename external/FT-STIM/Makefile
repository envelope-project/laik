# Makefile Recreated by Dai Yang
#
#	MQTT Client using Mosquitto for LAIK
# Dependencies: libprotobuf-c, libmosquitto
#
# (C) 2017 LRR, Technische Universitaet Muenchen
#

CC= cc
PROTOCC= protoc-c
CXX= c++
FLAGS= -std=gnu99
CFLAGS= -fPIC -Wall -g -Wextra -ggdb3
LDFLAGS= -lprotobuf-c -shared
DEBUGFLAGS= -O0 -D DEBUG
RELEASEFLAGS= -O2 -D NDEBUG -combine -fwhole-program
PROTOCFLAGS= --c_out=

PROTOS = $(wildcard *.proto)
TARGET  = liblaikfti.so
SOURCES = $(wildcard *.c)
HEADERS = $(wildcard *.h)
OBJECTS = $(SOURCES:.c=.o)
PROTOCOLC = $(PROTOS:.proto=.pb-c.c)
PROTOCOLH = $(PROTOS:.proto=.pb-c.h)

PROTODIR =.
PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin

all: $(TARGET)

$(PROTOCOLC): $(PROTOS)
	$(PROTOCC) $(PROTOCFLAGS)$(PROTODIR) $(PROTOS)

$(TARGET):  $(OBJECTS) $(PROTOCOLC)
	$(CC) $(FLAGS) $(CFLAGS) $(DEBUGFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJECTS)

clean:
	rm --force *.d *.o *.pb-c.c *.pb-c.h *.so

.PHONY: all clean

# Dependencies tracking:
-include *.d

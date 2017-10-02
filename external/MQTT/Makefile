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

CFLAGS = -std=gnu99 -I../../include -fPIC -Wall -Wextra
CFLAGS += $(OPT) $(DEFS) -MMD -MP

#PROTOCC= protoc-c
#LDFLAGS= -lprotobuf-c -lmosquitto -luuid -shared
LDFLAGS= -lmosquitto -shared
DEBUGFLAGS= -O0 -D DEBUG
RELEASEFLAGS= -O3 -D NDEBUG -combine -fwhole-program
#PROTOCFLAGS= --c_out=

#PROTOS = $(wildcard *.proto)
TARGET  = libmqttagent.so
SOURCES = mqttclient.c mqttagent.c
HEADERS = $(wildcard *.h)
OBJECTS = $(SOURCES:.c=.o)
#PROTOCOLC = $(PROTOS:.proto=.pb-c.c)
#PROTOCOLH = $(PROTOS:.proto=.pb-c.h)

#PROTODIR =.
PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin

#all: $(PROTOCOLC) $(TARGET)
all: $(TARGET)

#$(PROTOCOLC): $(PROTOS)
#	$(PROTOCC) $(PROTOCFLAGS)$(PROTODIR) $(PROTOS)

$(TARGET):  $(OBJECTS)
	$(CC) $(FLAGS) $(CFLAGS) $(DEBUGFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJECTS) 

clean:
	rm -f *.o
	rm -f *.d
	rm -f *.pb-c.*
	rm -f libmqttagent.so

.PHONY: all clean

# Dependencies tracking:
-include *.d

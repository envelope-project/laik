# Makefile Recreated by Dai Yang
#
#	MQTT Client using Mosquitto for LAIK
# Dependencies: libprotobuf-c, libmosquitto
#
# (C) 2017 LRR, Technische Universitaet Muenchen
#

include ../../Makefile.common

CFLAGS  += -I '../../include'
LDFLAGS += -shared
LDLIBS  += -lpapi

DEBUGFLAGS= -O0 -D DEBUG
RELEASEFLAGS= -O3 -D NDEBUG -combine -fwhole-program

TARGET  = libprofileagent.so
SOURCES = profilagent.c
HEADERS = $(wildcard *.h)
OBJECTS = $(SOURCES:.c=.o)

PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin

#all: $(PROTOCOLC) $(TARGET)
all: $(TARGET)

#$(PROTOCOLC): $(PROTOS)
#	$(PROTOCC) $(PROTOCFLAGS)$(PROTODIR) $(PROTOS)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o '$@' $+ $(LDLIBS)

clean:
	rm -f *.o
	rm -f *.d
	rm -f $(TARGET)

.PHONY: all clean

# Dependencies tracking:
-include *.d

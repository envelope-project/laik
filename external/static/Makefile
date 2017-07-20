# Makefile Recreated by Dai Yang
#
#	MQTT Client using Mosquitto for LAIK
# Dependencies: libprotobuf-c, libmosquitto
#
# (C) 2017 LRR, Technische Universitaet Muenchen
#

CC= cc
CXX= c++
FLAGS= -std=gnu99
CFLAGS= -fPIC -Wall -g -Wextra -ggdb3 -I../../include
LDFLAGS= -shared
DEBUGFLAGS= -O0 -D DEBUG
RELEASEFLAGS= -O3 -D NDEBUG -combine -fwhole-program 


TARGET  = liblaik_staticagent.so
SOURCES = $(wildcard *.c)
HEADERS = $(wildcard *.h)
OBJECTS = $(SOURCES:.c=.o)

PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin

all:  $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(FLAGS) $(CFLAGS) $(DEBUGFLAGS) $(LDFLAGS) $(INC) -o $(TARGET) $(OBJECTS)

clean:
	rm -f *.o
	rm -f *.d
	rm -f liblaik_staticagent.so

.PHONY: all clean

# Dependencies tracking:
-include *.d

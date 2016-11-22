CC= gcc
RM= rm -vf
CFLAGS= -Wall `pkg-config fuse --cflags --libs` -lzip -g
SRCFILES= $(wildcard *.c)
OBJFILES= $(patsubst %.c, %.o, $(SRCFILES))
PROGFILES= $(patsubst %.c, %, $(SRCFILES))

.PHONY: all clean

all: $(PROGFILES)
clean:
	$(RM) $(OBJFILES) $(PROGFILES) *~

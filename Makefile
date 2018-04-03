
# variables

CC = gcc
LD = gcc
CFLAGS = -Wall
LDFLAGS =
INCLUDEDIRS = -I.
LIBS =


# implicit rules

%.o:	%.c
	$(CC) -c $(CFLAGS) $(INCLUDEDIRS) $< -o $@

# source files

SRC = adpcm.c es12wav.c
H = adpcm.h
OBJS = $(SRC:.c=.o)

# targets

all:	es12wav

es12wav:	$(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

zip:	es12wav.zip

es12wav.zip:	$(SRC) $(H) Makefile
	zip es12wav.zip $^ es12wav

clean:
	rm -f *.o es12wav

# file dependencies

es12wav.o:	es12wav.c adpcm.h
adpcm.o:	adpcm.c adpcm.h

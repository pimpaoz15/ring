#Compiler, Compiler Flags
CC = gcc
CFLAGS =-Wall -std=c99 -O3

#Sources
SOURCES = ring.c

#Objects ('make' automatically compiles .c to .o)
OBJECTS = ring.o

ring: $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

ring.o: ring.c

clean:
		rm -f *.o ring

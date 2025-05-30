##############################################################################
#
#                     Makefile for gpib.so
#
##############################################################################

# This variable contains the flags passed to cc.

CC = gcc -g
AR = ar
F77 = g77
CFLAGS = -m32 -fPIC -Di686 -w -DUSB_GPIB_RENAME -DUSB_POLL
LIBS = $(LABVIEW_LIB)
OBJ = o
SUFFIX = so
INCLUDES = -I./
DLLFLAGS = -shared -fno-gnu-linker -m32
.SUFFIXES:
.SUFFIXES: .c .o .so .a

OBJECTS = gpib.$(OBJ)


.c:
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBS)

.c.o: 
	$(CC) $(INCLUDES) -c $(CFLAGS) $<

.o.a: 
	$(AR) -r $@ $< 

all:    gpib.$(SUFFIX)

static:    gpib.a

gpib.a:    gpib.so

gpib.so:    $(OBJECTS)
	$(CC) $(DLLFLAGS) -o $@ $(OBJECTS) $(LIBS)

clean:
	 rm -f\
	 gpib.$(SUFFIX) *.$(OBJ)

dist:
	 tar cvfz gpib.tgz *.c *.h Makefile *.llb

test:    gpib.$(SUFFIX)
	 $(CC) test.c -o test -m32 -Di686 gpib.$(SUFFIX)  $(INCLUDES)


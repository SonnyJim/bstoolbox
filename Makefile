CC = cc
OBJS = bstoolbox.c

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	CFLAGS = -O2
	OBJS += linux.c
endif
ifeq ($(UNAME_S),IRIX)
	CFLAGS = -mips3 -n32 -O2
	OBJS += irix.c
endif

default all: bstoolbox

bstoolbox: $(OBJS) Makefile
	@echo "*** Compiling: $@ (OS: $(UNAME_S))"
	@$(CC) -o $@ $(CFLAGS) $(OBJS)

clean:
	@echo "*** Cleaning up..."
	@-rm -f bstoolbox core *.o

CC = cc
CFLAGS = -mips3 -n32 -O2

default all: bstoolbox

bstoolbox: bstoolbox.c irix.c Makefile
	@echo "*** Compiling: $@"
	@$(CC) -o $@ $(CFLAGS) bstoolbox.c irix.c

clean:
	@echo "*** Cleaning up..."
	@-rm -f bstoolbox core

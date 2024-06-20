CC = cc
CFLAGS = -mips3 -n32 -O2

default all: bstoolbox

bstoolbox: bstoolbox.c Makefile
	@echo "*** Compiling: $@"
	@$(CC) -o $@ $(CFLAGS) bstoolbox.c

clean:
	@echo "*** Cleaning up..."
	@-rm -f bstoolbox core

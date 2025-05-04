CC = cc
CFLAGS =
OBJS =

# Default target
default: bstoolbox

# Use shell to detect OS and set CFLAGS and OBJS
bstoolbox:
	@OS=`uname -s`; \
	if [ "$$OS" = "Linux" ]; then \
		CFLAGS="-O2 -DOS_LINUX"; OBJS="bstoolbox.c linux.c"; \
	elif [ "$$OS" = "IRIX64" ]; then \
		CFLAGS="-mips3 -n32 -O2 -DOS_IRIX"; OBJS="bstoolbox.c irix.c"; \
	else \
		echo "Unsupported OS: $$OS"; exit 1; \
	fi; \
	echo "*** Compiling: bstoolbox (OS: $$OS)"; \
	$(CC) -o bstoolbox $$CFLAGS $$OBJS

clean:
	@echo "*** Cleaning up..."
	@-rm -f bstoolbox core *.o


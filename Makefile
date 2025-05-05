CC = cc
CFLAGS =
LDFLAGS =
SRCS =
OBJS =

# Default target
default: detect

# OS detection and conditional build
detect:
	@OS=`uname -s`; \
	if [ "$$OS" = "Linux" ]; then \
		echo "*** Compiling for Linux"; \
		$(MAKE) bstoolbox \
			BUILD_OS=LINUX \
			SRCS="bstoolbox.c linux.c" \
			OBJS="bstoolbox.o linux.o" \
			CFLAGS="-O2 -DOS_LINUX" \
			LDFLAGS=""; \
	elif [ "$$OS" = "IRIX64" ]; then \
		echo "*** Compiling for IRIX64"; \
		$(MAKE) bstoolbox \
			BUILD_OS=IRIX \
			SRCS="bstoolbox.c irix.c" \
			OBJS="bstoolbox.o irix.o" \
			CFLAGS="-mips3 -n32 -O2 -DOS_IRIX" \
			LDFLAGS=""; \
	else \
		echo "Unsupported OS: $$OS"; exit 1; \
	fi

# Build target
bstoolbox: $(OBJS)
	$(CC) $(CFLAGS) -o bstoolbox $(OBJS) $(LDFLAGS)

# Object file rules
bstoolbox.o: bstoolbox.c
	$(CC) $(CFLAGS) -c bstoolbox.c

irix.o: irix.c
	$(CC) $(CFLAGS) -c irix.c

linux.o: linux.c
	$(CC) $(CFLAGS) -c linux.c

# Clean rule
clean:
	@echo "*** Cleaning up..."
	@-rm -f *.o bstoolbox core


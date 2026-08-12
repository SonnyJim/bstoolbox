CC = cc
CFLAGS =
LDFLAGS =

# Default target
default: detect

# OS detection and conditional build
detect:
	@OS=`uname -s`; \
	if [ "$$OS" = "Linux" ]; then \
		echo "*** Compiling for Linux"; \
		$(MAKE) all \
			BUILD_OS=LINUX \
			OS_OBJ="linux.o" \
			CFLAGS="-O2 -DOS_LINUX" \
			LDFLAGS=""; \
	elif [ "$$OS" = "IRIX64" ] || [ "$$OS" = "IRIX" ]; then \
		echo "*** Compiling for IRIX"; \
		$(MAKE) all \
			BUILD_OS=IRIX \
			OS_OBJ="irix.o" \
			CFLAGS="-mips3 -n32 -O2 -DOS_IRIX" \
			LDFLAGS=""; \
	else \
		echo "Unsupported OS: $$OS"; exit 1; \
	fi

all: bstoolbox bswifi

# Build targets
bstoolbox: bstoolbox.o $(OS_OBJ)
	$(CC) $(CFLAGS) -o bstoolbox bstoolbox.o $(OS_OBJ) $(LDFLAGS)

bswifi: bswifi.o $(OS_OBJ)
	$(CC) $(CFLAGS) -o bswifi bswifi.o $(OS_OBJ) $(LDFLAGS)

# Object file rules
bstoolbox.o: bstoolbox.c bstoolbox.h
	$(CC) $(CFLAGS) -c bstoolbox.c

bswifi.o: bswifi.c
	$(CC) $(CFLAGS) -c bswifi.c

irix.o: irix.c os.h
	$(CC) $(CFLAGS) -c irix.c

linux.o: linux.c os.h
	$(CC) $(CFLAGS) -c linux.c

# Clean rule
clean:
	@echo "*** Cleaning up..."
	@-rm -f *.o bstoolbox bswifi core

# Specify sliding attacks generation method. One of -DMAGIC, -DHYPER, -DBMI2.
# -DBMI2 only works on Haswell processors.
# -DHYPER requires a processor with SSE3 support.
# -DMAGIC and -DHYPER are about equally fast on my system.
FLAGS = -DMAGIC
#FLAGS = -DHYPER
#FLAGS = -DBMI2

# Specify -DUSE_POPCNT on a machine with popcnt support.
# For pawnful tables this seems slightly faster. For pawnless tables it
# does not seem to matter.
FLAGS += -DUSE_POPCNT

# Endianness should be detected automatically, but if that does not work
# then specify -DARCH_BIG to compile for a big endian platform.
#FLAGS += -DARCH_BIG

# Override maximum number of threads supported. Default is 12.
#FLAGS += -DMAX_THREADS=32

# Specify the compiler
# regular linux
CC = gcc -pthread
# cross compiling from Linux to Windows
#CC = x86_64-w64-mingw32-gcc

# Compilation flags
# Remove or replace -march=corei7 and -msse4.2 if necessary.
# For Haswell add -mbmi2 and select -DBMI2 above.
#CFLAGS = -g -pipe -D_GNU_SOURCE -Wall -std=c11 -Wstrict-aliasing=2
CFLAGS = -g -O3 -pipe -D_GNU_SOURCE -Wall -std=c11 -Wstrict-aliasing=2 -flto

export FLAGS CC CFLAGS

all: rtbgen rtbgenp tbcheck

rtbgen rtbgenp rtbver rtbverp tbcheck clean:
	@$(MAKE) -f Makefile.regular $@

.PHONY: rtbgen rtbgenp rtbver rtbverp tbcheck clean


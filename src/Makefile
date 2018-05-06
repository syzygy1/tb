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

# Specify maximum number of pieces. More than 7 won't work.
FLAGS += -DTBPIECES=7

# Use libzstd instead of LZ4 for compression of temporary files.
FLAGS += -DUSE_ZSTD
LDFLAGS += -lzstd

# Specify number of threads used for compression of temporary files.
FLAGS += -DCOMPRESSION_THREADS=6

# Specify the compiler
# regular linux
#CC = /usr/bin/gcc -pthread
CC = gcc -pthread
# cross compiling from Linux to Windows
#CC = x86_64-w64-mingw32-gcc

# Compilation flags
# For Haswell add -mbmi2 and select -DBMI2 above.
#CFLAGS = -g -march=native -pipe -D_GNU_SOURCE -Wall -std=c11 -Wno-array-bounds
CFLAGS = -O3 -march=native -pipe -D_GNU_SOURCE -Wall -std=c11 -Wno-array-bounds -flto
#CFLAGS = -O3 -march=native -pipe -D_GNU_SOURCE -Wall -std=c11 -fsanitize=undefined -Wno-array-bounds

export FLAGS CC CFLAGS LDFLAGS

all: rtbgen rtbgenp rtbver rtbverp tbcheck

rtbgen rtbgenp rtbver rtbverp tbcheck clean:
	@$(MAKE) -f Makefile.regular $@

atbgen atbgenp atbver atbverp aclean:
	@$(MAKE) -f Makefile.atomic $@

stbgen stbgenp sclean:
	@$(MAKE) -f Makefile.suicide $@

gtbgen gtbgenp gclean:
	@$(MAKE) -f Makefile.giveaway $@

atomic: atbgen atbgenp atbver atbverp tbcheck

suicide: stbgen stbgenp tbcheck

giveaway: gtbgen gtbgenp tbcheck

.PHONY: rtbgen rtbgenp rtbver rtbverp tbcheck clean \
	atbgen atbgenp atbver atbverp aclean \
	stbgen stbgenp sclean \
	gtbgen gtbgenp gclean

# Specify sliding attacks generation method. One of -DMAGIC, -DHYPER, -DBMI2.
# -DBMI2 requires a processor with (fast) BMI2 support.
# -DHYPER requires a processor with SSE3 support.
#FLAGS = -DMAGIC
#FLAGS = -DHYPER
FLAGS = -DBMI2

# Specify -DUSE_POPCNT on a machine with popcnt support.
# For pawnful tables this seems slightly faster. For pawnless tables it
# does not seem to matter.
FLAGS += -DUSE_POPCNT

# Specify maximum number of pieces. More than 7 won't work.
FLAGS += -DTBPIECES=7

# Use libzstd instead of LZ4 for compression of temporary files, and set
# compression level.
FLAGS += -DUSE_ZSTD -DZSTD_LEVEL=6
LDFLAGS += -lzstd

# Specify number of threads used for (de)compression of temporary files.
FLAGS += -DCOMPRESSION_THREADS=6

# Specify the compiler
# regular linux
CC = gcc -pthread
# cross compiling from Linux to Windows
#CC = x86_64-w64-mingw32-gcc -pthread

# Compilation flags
#CFLAGS = -g3 -march=native -pipe -D_GNU_SOURCE -Wall -std=c11
CFLAGS = -O3 -march=native -pipe -D_GNU_SOURCE -Wall -std=c11 -flto=auto
#CFLAGS = -O3 -march=native -pipe -D_GNU_SOURCE -Wall -std=c11 -fsanitize=undefined

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

jtbgen jtbgenp jclean:
	@$(MAKE) -f Makefile.shatranj $@

#atbgen atbgenp atbver atbverp aclean:
atomic: atbgen atbgenp atbver atbverp tbcheck

suicide: stbgen stbgenp tbcheck

giveaway: gtbgen gtbgenp tbcheck

shatranj: jtbgen jtbgenp tbcheck

.PHONY: rtbgen rtbgenp rtbver rtbverp tbcheck clean \
	atbgen atbgenp atbver atbverp aclean \
	stbgen stbgenp sclean \
	gtbgen gtbgenp gclean \
	jtbgen jtbgenp jclean

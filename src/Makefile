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

# Specify the compiler
# regular linux
#CC = /usr/bin/gcc -pthread
CC = gcc -pthread
# cross compiling from Linux to Windows
#CC = x86_64-w64-mingw32-gcc

# Compilation flags
# Remove or replace -march=corei7 and -msse4.2 if necessary.
# For Haswell add -mbmi2 and select -DBMI2 above.
#CFLAGS = -g -march=native -pipe -D_GNU_SOURCE -Wall -std=c99 -Wno-array-boungs -flto
CFLAGS = -O3 -march=native -pipe -D_GNU_SOURCE -Wall -std=c99 -Wno-array-bounds -flto
#CFLAGS = -O3 -march=native -pipe -D_GNU_SOURCE -Wall -std=c99 -fsanitize=undefined -Wno-array-bounds -flto -fprofile-use -lgcov
#LDFLAGS = -lubsan
LDFLAGS = 

export FLAGS CC CFLAGS LDFLAGS

rtbgen rtbgenp rtbver rtbverp tbcheck clean:
	@$(MAKE) -f Makefile.regular $@

atbgen atbgenp atbver atbverp aclean:
	@$(MAKE) -f Makefile.atomic $@

all: rtbgen rtbgenp rtbver rtbverp tbcheck

atomic: atbgen atbgenp atbver atbverp tbcheck

.PHONY: rtbgen rtbgenp rtbver rtbverp tbcheck clean \
	atbgen atbgenp atbver atbverp aclean


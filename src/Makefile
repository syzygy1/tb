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
CC = gcc -pthread
# cross compiling from Linux to Windows
#CC = x86_64-w64-mingw32-gcc

export FLAGS CC

rtbgen rtbgenp rtbver rtbverp tbcheck clean:
	@$(MAKE) -f Makefile.regular $@

all: rtbgen rtbgenp rtbver rtbverp tbcheck

.PHONY: rtbgen rtbgenp rtbver rtbverp tbcheck clean


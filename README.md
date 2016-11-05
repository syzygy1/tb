### Overview

Included are a tablebase generator and probing code for adding tablebase
probing to a chess engine. The tablebase generator is able to generate
all tablebases for up to 6 pieces.

Requirements for the generator:
* 16 GB of RAM for 6-piece tables (much less for 5-piece tables).
* x86-64 CPU.
* 64-bit OS.
* Sufficiently recent gcc (producing 64-bit executables).


### Tablebase files

File names encode the type of tablebase: K+R+P vs K+R becomes KRPvKR.
Each tablebase corresponds to two files: KRPvKR.rtbw and KRPvKR.rtbz.
Note that KRPvKR also covers K+R vs K+R+P.

The .rtbw files store win/draw/loss information including, where applicable,
information on the 50-move rule. During the search only the .rtbw files
are accessed. These files are "two-sided": they store information for both
white to move and black to move.

The .rtbz files store the distance-to-zero: the number of moves to the next
capture or pawn move. These files only need to be accessed when the root
position has 6 pieces or less. They are "single-sided".

The 6-piece WDL tables are 68.2 GB in total. The DTZ tables take up 81.9 GB.
For up to 5 pieces, the numbers are 378 MB and 561 MB. Ideally, the WDL
tables are stored on an SSD.


### Tablebase generator

The directory src/ contains the tablebase generator code. It should be
easy to build on x86-64 Linux system and on 64-bit Windows with MinGW
("make all"). It might be necessary to edit src/Makefile. In particular,
**if your CPU does not support the popcnt instruction**, the line `FLAGS +=
-DUSE_POPCNT` should be commented out.

There are five programs:
* rtbgen for generating pawnless tablebases.
* rtbgenp for generating pawnful tablebases.
* rtbver for verifying pawnless tablebases.
* rtbverp for verifying pawnful tablebases.
* tbcheck for verifying integrity of tablebase files based on an embedded
checksum.

**Note 1:** Since a correct set of checksums is known, there is no need for anyone to run rtbver and rtbverp.

**Note 2:** The checksums are **not** md5sums. However, correct md5sums are known as well, and these can also be used to verify integrity. See http://kirill-kryukov.com/chess/tablebases-online/

**Usage:** `rtbgen KQRvKR`   (or `rtbgenp KRPvKR`)  
Produces two compressed files: KQRvKR.rtbw and KQRvKR.rtbz. Both files
contain an embedded checksum.  

**Options:**  
--threads n  (or -t n)  
Use n threads. If no other tasks are running, it is recommended to
set n to the number of CPU cores, or if the CPU supports hyperthreading,
to the number of CPU hyperthreads.

--wdl  (or -w)  
Only compress and save the WDL file (with .rtbw suffix).

--dtz  (or -z)  
Only compress and save the DTZ file (with .rtbz suffix).

-g  
Generate the table but do not compress and save.

--stats  (or -s)  
Save statistics. Statistics are written to $RTBSTATSDIR/KQRvKR.txt
or to ./KQRvKR.txt if $RTBSTATSDIR is not set.

--disk  (or -d)  
Reduce RAM usage during compression. This takes a bit more time because
tables are temporarily saved to disk. **This option is necessary to
generate 6-piece tables on systems with 16 GB RAM.** This option is
not needed on systems with 24 GB RAM or more.

**Usage:** `rtbver KQRvKR`   (or `rtbverp KRPvKR`)  
Verifies consistency of KQRvKR.rtbw and KQRvKR.rtbz. This should detect
(hardware) errors during generation and compression. For technical reasons
pawnful tables with symmetric material such as KPvKP and KRPvKRP cannot
(at least currently) be verified.

**Options:**  
--threads n  (or -t n)  
See above.

--log  (or -l)  
Log verification results to rtblog.txt.

-d  
Look for the WDL file in directory $RTBWDIR and look for the DTZ file in
directory $RTBZDIR. Without this option, both files should be present in
the current working directory.

**Usage:** `tbcheck KQRvKR.rtbw KRPvKR.rtbz`  
Recalculates a checksum for each specified tablebase file and compares with
the embedded checksums. This should detect disk errors and transmission
errors.

**Options:**  
--threads n  (or -t n)  
See above.

--print  (or -p)  
Print embedded checksums. Do not check correctness.

**Alternative usage:** `tbcheck --compare wdl345.txt`
Compares the embedded checksum for each tablebase file listed in wdl345.txt
with the checksum specified in wdl345.txt. Note that these are not md5sums.

Note: The programs rtbgen, rtbgenp, rtbver and rtbverp require access
to WDL tablebase files for "subtables". These should be present in
the directory $RTBWDIR. The program tbcheck only looks in the current working
directory.


### Scripts

The somewhat primitive perl script src/run.pl can be used for generating
and verifying all or part of the tables. Make sure the location of rtbgen,
rtbgenp, rtbver and rtbverp is in your $PATH variable.

**Usage:** `run.pl --generate`

**Options:**  
--threads n  (or -t n)  
See above.

--generate  
Generate tablebases. Tablebases that already have been generated and are
found in the current working directory are skipped.

--verify  
Verify tablebases.

--min n  
Only treat tablebases with at least n pieces.

--max n  
Only treat tablebases with at most n pieces.

--disk  
Use this option to generate 6-piece tables on a system with 16 GB of RAM.


### Probing code

The directory interface/ contains probing code. It does not come in the
form of a shared library, and requires some work to integrate into an
engine. The main reason for this is efficiency. There are four files:
tbcore.c, tbcore.h, tbprobe.cpp, tbprobe.h.

The files tbcore.c and tbcore.h should not require many changes, although
engine authors might want to replace some printf()s with suitable logging
statements. The files tbprobe.cpp and tbprobe.h do require some changes
but these should be fairly straightforward when following the comments.
The only reason for tbprobe.cpp having the .cpp extension is that I have
used Stockfish as example. The probing code is initialised by calling
init_tablebases(path), where path contains the directories (separated with
a colon on Linux and with a semicolon on Windows) where the WDL and DTZ
files are to be found.

The files main.cpp, search.cpp and types.h are from Stockfish with calls
to the probing code added (see // TB comments). The change in types.h is
necessary in order to make room for "tablebase win in n" values distinct
from "mate in n" values. Please note that the integration of probing code
into Stockfish is merely intended as a proof of concept.

Note that when properly integrating the interface code in an engine that is
not Stockfish, no trace of Stockfish will be left. The engine author will
have to rewrite the Stockfish-specific glueing code to match his or her
engine. Therefore no copyright issues can arise (see also below).


### Terms of use

The files lz4.c and lz4.h in src/ are copyrighted by Yann Collet and were
released under the BSD 2-Clause License. The files city-c.c, city-c.h and
citycrc.h in src/ (ported by me from C++ to C) are copyrighted by Google,
Inc. and were released under an even more liberal license. Both licenses
are compatible with the GPL. All other files in src/ are released under
the GNU Public License, version 2 (only).

The files main.cpp, search.cpp and types.h in interface/ obviously are
copyrighted by the Stockfish authors and covered by the Stockfish GPL with
the exception of the code fragments preceded by // TB comments. These
fragments may be freely modified and redistributed in source and/or binary
format.

All tablebase files generated using this generator may be freely redistributed.
In fact, those files are free of copyright at least under US law (following
Feist Publications, Inc., v. Rural Telephone Service Co., 499 U.S. 340 (1991))
and under EU law (following Football Dataco and Others v. Yahoo! UK Ltd and
Others (C-604/10)).


Ronald de Man  
syzygy_tb@yahoo.com


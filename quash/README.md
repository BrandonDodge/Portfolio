Quash: A Simple Unix-Like Shell



Course: EECS 678 — Operating Systems

Author: Brandon Dodge and Kevinh Nguyen

Environment: Ubuntu 24.04 (WSL2)

Compiler: GCC 11+ (GNU C11 standard)



Overview



Quash is a lightweight, educational shell written in C that replicates core UNIX shell functionality.

It supports basic command execution, piping, background jobs, I/O redirection, environment variable management, and built-in commands like cd, pwd, echo, export, and kill.



This implementation is fully tested, memory-leak–free (verified via Valgrind), and uses a clean modular design separating parsing logic (quash.c) from utility helpers (util.c, util.h).



Features



Command parsing with token expansion and variable substitution ($VAR)



Built-in commands:



cd – Change working directory



pwd – Print current working directory



echo – Print arguments



export – Set environment variables



jobs – List background jobs



kill – Send signals to processes



exit – Terminate shell



Pipelining (|)



I/O redirection (> and <)



Background execution (\&)



Job tracking and completion notifications



No memory leaks (verified via Valgrind)


File Structure

src/

├── quash.c          # Core shell logic, command parsing, process management

├── util.c           # Memory safety helpers and string functions

├── util.h           # Header definitions for util.c

├── test\_quash.sh    # Automated test script

├── Makefile         # Build, test, and Valgrind automation

└── README.md        # Project documentation



Build Instructions



To build the shell:



make clean \&\& make





To remove all build artifacts:



make clean



Usage



Launch Quash interactively:



make run





Example commands:



\[QUASH] $ pwd

/mnt/c/Users/ZE3Z/Desktop/quash-main/src

\[QUASH] $ echo hello world

hello world

\[QUASH] $ export FOO=bar

\[QUASH] $ echo $FOO

bar

\[QUASH] $ sleep 3 \&

Background job started: \[1] 1234 sleep 3 \&

\[QUASH] $ jobs

\[1] 1234 sleep 3 \&

Completed: \[1] 1234 sleep 3 \&

\[QUASH] $ exit



Testing



To automatically verify core functionality:



make test





This runs test\_quash.sh, which checks:



Command parsing



Environment exports



Directory changes



Background job handling



Piping and redirection



Signal handling (kill tests)



Expected result:



\[OK] pwd prints path

\[OK] echo works

\[OK] export + echo $FOO

\[OK] cd updates cwd/PWD

\[OK] jobs shows bg

\[OK] completed prints

\[OK] pipe finds token

\[OK] all tests done



Memory Validation



Run a full Valgrind memory test:



make vg





This executes Quash under Valgrind with several representative commands.

Expected output:



== VALGRIND SUMMARY ==

All heap blocks were freed — no leaks are possible

ERROR SUMMARY: 0 errors from 0 contexts


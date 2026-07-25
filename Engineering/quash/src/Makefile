# Makefile — EECS 678: Quash
# Builds the shell, runs tests, checks Valgrind, and packages a tarball.
.RECIPEPREFIX := >

CC       := gcc
CFLAGS   := -std=gnu11 -Wall -Wextra -Werror=implicit-function-declaration -O2 -g
LDFLAGS  :=
OBJS     := quash.o util.o

.PHONY: all clean run test vg help package

# Default build
all: quash

# Link final binary
quash: $(OBJS)
> $(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

# Compile units
quash.o: quash.c util.h
> $(CC) $(CFLAGS) -c quash.c

util.o: util.c util.h
> $(CC) $(CFLAGS) -c util.c

# Run interactively
run: quash
> ./quash

# Smoke tests
test: quash test_quash.sh
> bash ./test_quash.sh ./quash

# Valgrind (no heredoc; feed script via printf so it works in make)
vg: quash
> printf '%s\n' \
>   "echo hi > t.txt" \
>   "cat < t.txt | grep hi" \
>   "export FOO=bar" \
>   "echo $$FOO" \
>   "sleep 1 &" \
>   "sleep 2" \
>   "exit" \
> | valgrind --leak-check=full --show-leak-kinds=all \
>     --errors-for-leak-kinds=definite,indirect,possible \
>     --track-origins=yes ./quash
> rm -f t.txt

# Package for submission
package: clean
> tar -czf quash-$(shell whoami)-$(shell date +%Y%m%d).tar.gz \
>   quash.c util.c util.h Makefile test_quash.sh README.md

# Help text
help:
> @echo "Targets:"
> @echo "  make            - build quash"
> @echo "  make run        - run the shell interactively"
> @echo "  make test       - run smoke tests"
> @echo "  make vg         - run Valgrind on a small script"
> @echo "  make package    - create a tar.gz for submission"
> @echo "  make clean      - remove build artifacts"

# Clean artifacts
clean:
> rm -f quash *.o

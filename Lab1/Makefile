EXECBIN = myweb
SOURCES = src/myweb.c
OBJECTS = src/myweb.o

CFLAGS   = -Wall -Werror -Wextra -Wpedantic -Wstrict-prototypes

all: $(EXECBIN)

$(EXECBIN): $(OBJECTS)
	clang -o bin/$@ $^

src/%.o : %.c
	clang $(CFLAGS) -c $<

clean:
	rm -f bin/$(EXECBIN) $(OBJECTS)

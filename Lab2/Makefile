EXECBIN = myweb
SOURCES = src/myweb.c src/utils.c
OBJECTS = src/myweb.o src/utils.o

CFLAGS   = -Wall -Werror -Wextra -Wpedantic -Wstrict-prototypes

all: $(EXECBIN)

$(EXECBIN): $(OBJECTS)
	gcc -o bin/$@ $^

src/%.o : %.c
	gcc $(CFLAGS) -c $<

clean:
	rm -f bin/$(EXECBIN) $(OBJECTS)

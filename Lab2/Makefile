EXECBIN = myclient myserver
SOURCES = src/myclient.c src/myserver.c src/utils.c
OBJECTS = src/myclient.o src/myserver.o src/utils.o

CFLAGS   = -Wall -Werror -Wextra -Wpedantic -Wstrict-prototypes

all: $(EXECBIN)

client: myclient
server: myserver

$(EXECBIN): $(OBJECTS)
	gcc -o bin/$@ $^

src/%.o : %.c
	gcc $(CFLAGS) -c $<

clean:
	rm -f bin/$(EXECBIN) $(OBJECTS)

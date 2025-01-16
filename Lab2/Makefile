CLIENT_BIN = myclient
CLIENT_OBJS = src/myclient.o src/utils.o
SERVER_BIN = myserver
SERVER_OBJS = src/myserver.o src/utils.o

OBJECTS = src/myclient.o src/myserver.o src/utils.o

CFLAGS   = -Wall -Werror -Wextra -Wpedantic -Wstrict-prototypes

all: $(EXECBIN)

client: 
CLIENT_BIN: 
server: myserver

$(EXECBIN): $(OBJECTS)
	gcc -o bin/$@ $^

src/%.o : %.c
	gcc $(CFLAGS) -c $<

clean:
	rm -f bin/$(EXECBIN) $(OBJECTS)

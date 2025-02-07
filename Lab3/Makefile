CLIENT_BIN = myclient
CLIENT_OBJS = src/myclient.o src/utils.o
SERVER_BIN = myserver
SERVER_OBJS = src/myserver.o src/utils.o src/client_info.o

OBJECTS = src/myclient.o src/myserver.o src/utils.o

CFLAGS = -Wall -Werror -Wextra -Wpedantic -Wstrict-prototypes

all: $(CLIENT_BIN) $(SERVER_BIN)

$(CLIENT_BIN): $(CLIENT_OBJS)
	mkdir -p bin
	gcc -o bin/$@ $^

$(SERVER_BIN): $(SERVER_OBJS)
	mkdir -p bin
	gcc -o bin/$@ $^

src/%.o : %.c
	gcc $(CFLAGS) -c $<

clean:
	rm -f bin/$(CLIENT_BIN) bin/$(SERVER_BIN) $(CLIENT_OBJS) $(SERVER_OBJS)

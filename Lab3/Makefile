CLIENT_BIN = myclient
CLIENT_OBJS = src/myclient.o src/utils.o
SERVER_BIN = myserver
SERVER_OBJS = src/myserver.o src/utils.o src/client_info.o
CRASHING_SERVER_BIN = crashingserver
CRASHING_SERVER_OBJS = src/crashingserver.o src/utils.o src/client_info.o
OOO_SERVER_BIN = oooserver
OOO_SERVER_OBJS = src/oooserver.o src/utils.o

OBJECTS = src/myclient.o src/myserver.o src/utils.o

CFLAGS = -Wall -Werror -Wextra -Wpedantic -Wstrict-prototypes

all: $(CLIENT_BIN) $(SERVER_BIN)

$(CLIENT_BIN): $(CLIENT_OBJS)
	gcc -o bin/$@ $^

$(SERVER_BIN): $(SERVER_OBJS)
	gcc -o bin/$@ $^

$(CRASHING_SERVER_BIN): $(CRASHING_SERVER_OBJS)
	gcc -o bin/$@ $^

$(OOO_SERVER_BIN): $(OOO_SERVER_OBJS)
	gcc -o bin/$@ $^

src/%.o : %.c
	gcc $(CFLAGS) -c $<

clean:
	rm -f bin/$(CLIENT_BIN) bin/$(SERVER_BIN) bin/$(CRASHING_SERVER_BIN) bin/$(OOO_SERVER_BIN) $(CLIENT_OBJS) $(SERVER_OBJS) $(CRASHING_SERVER_OBJS) $(OOO_SERVER_OBJS)

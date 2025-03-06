PROXY_BIN = myproxy
PROXY_OBJS = src/myproxy.o src/utils.o

CFLAGS = -Wall -Werror -Wextra -Wpedantic -Wstrict-prototypes

all: $(PROXY_BIN)

$(PROXY_BIN): $(PROXY_OBJS)
	mkdir -p bin
	gcc -pthread -o bin/$@ $^

src/%.o : %.c
	gcc $(CFLAGS) -c $<

clean:
	rm -f bin/$(PROXY_BIN) $(PROXY_OBJS)

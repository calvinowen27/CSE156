PROXY_BIN = myproxy
PROXY_OBJS = src/myproxy.o src/utils.o

CFLAGS = -Wall -Werror -Wextra -Wpedantic -Wstrict-prototypes

all: $(PROXY_BIN)

$(PROXY_BIN): $(PROXY_OBJS)
	mkdir -p bin
	gcc -o bin/$@ $^ -L/opt/homebrew/lib -lssl -lcrypto
# gcc -o bin/$@ $^ -L/usr/lib/aarch64-linux-gnu -lssl

src/%.o : %.c
	gcc $(CFLAGS) -c $<

clean:
	rm -f bin/$(PROXY_BIN) $(PROXY_OBJS)

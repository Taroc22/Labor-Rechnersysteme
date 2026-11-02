CC       ?= cc
CPPFLAGS := -DMD_SUPPORT=1 -Iinclude -Itrdp/include
CFLAGS   := -Wall -Wextra -O2
LDLIBS   := trdp/lib/libtrdp.a -lpthread

SRC      := src/main.c src/pd_communication.c
TARGET   := build/pd-receiver

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) $(LDLIBS) -o $@

build:
	mkdir -p $@

clean:
	rm -rf build
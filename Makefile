CC      := gcc
INCLUDES := -I./src/core -I./src/models
CFLAGS  := -Wall $(INCLUDES) `pkg-config --cflags gtk4 libqrencode libcurl`
LIBS    := `pkg-config --libs gtk4 libqrencode libcurl`

SRCS_CORE    := $(wildcard ./src/core/*.c)

main.exe: $(SRCS_CORE)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

.PHONY: clean
clean:
	rm -f main.exe

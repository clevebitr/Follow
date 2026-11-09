CC      := gcc
INCLUDES := -I./src/core -I./src/models
PKGS    := gtk4 libqrencode libcurl json-glib-1.0 librsvg-2.0 libavformat libavcodec libswresample libavutil
CFLAGS  := -Wall $(INCLUDES) `pkg-config --cflags $(PKGS)`
LIBS    := `pkg-config --libs $(PKGS)`

SRCS_CORE    := $(wildcard ./src/core/*.c)

main.exe: $(SRCS_CORE)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

.PHONY: clean
clean:
	rm -f main.exe

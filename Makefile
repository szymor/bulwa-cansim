.PHONY: all clean

PROJECT=bulwa

all: $(PROJECT)

$(PROJECT): main.c
	gcc -o $(PROJECT) $^ `pkg-config --cflags --libs lua`

clean:
	rm -rf $(PROJECT)

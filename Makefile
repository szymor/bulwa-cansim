.PHONY: all clean

PROJECT=bulwa

all: $(PROJECT)

$(PROJECT): main.c config.c
	gcc -o $(PROJECT) $^ `pkg-config --cflags --libs lua libcjson`

clean:
	rm -rf $(PROJECT)

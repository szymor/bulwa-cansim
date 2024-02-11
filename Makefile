.PHONY: all clean

PROJECT=bulwa
SRC=main.c config.c luaenv.c
INC=global.h

all: $(PROJECT)

$(PROJECT): $(SRC) $(INC)
	gcc -o $(PROJECT) $(SRC) `pkg-config --cflags --libs lua libcjson`

clean:
	rm -rf $(PROJECT)

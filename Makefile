.PHONY: all clean

PROJECT=bulwa
SRC=$(addprefix src/,main.c config.c luaenv.c)
INC=$(addprefix src/,global.h)

all: $(PROJECT)

$(PROJECT): $(SRC) $(INC)
	gcc -o $(PROJECT) $(SRC) `pkg-config --cflags --libs lua libcjson`

clean:
	rm -rf $(PROJECT)

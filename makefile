all:
	clang main.c -o segfaultos \
	-I/opt/homebrew/include \
	-L/opt/homebrew/lib \
	-lraylib \
	-framework OpenGL \
	-framework Cocoa \
	-framework IOKit \
	-framework CoreVideo

run: all
	./segfaultos

clean:
	rm -f segfaultos

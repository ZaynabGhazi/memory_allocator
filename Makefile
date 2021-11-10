mem:	mem.h	mem.c
	gcc -c -g -Wall -fpic mem.c; gcc -g -shared -o libmem.so mem.o;
clean:
	rm mem.o libmem.so	
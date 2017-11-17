all: a.out

a.out: answer.c
	gcc -g -O1 -pthread -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-int-conversion answer.c

clean:
	rm -f a.out

.PHONY: all clean

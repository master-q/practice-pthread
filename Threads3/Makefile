all: a.out

a.out: answer.c queue.h
	gcc -g -O1 -pthread -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-int-conversion answer.c

zip:
	zip answer-kiwamu.zip answer.c queue.h

clean:
	rm -f a.out

.PHONY: all clean zip

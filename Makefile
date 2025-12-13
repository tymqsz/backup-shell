all: main

main: main.c fileproc.c synchro.c
	gcc main.c fileproc.c synchro.c -o main

.PHONY:

clean:
	rm main
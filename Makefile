master: master.c
	gcc -g -o master master.c -I.

palin: palin.c
	gcc -g -o palin palin.c -I.

clean: 
	rm palin master palin.out nopalin.out

.PHONY : all linux mingw

all : linux

linux :
	gcc -g -Wall -fPIC --shared -o snapshot.so snapshot.c

mingw :
	gcc -g -Wall --shared -o snapshot.dll snapshot.c -Ilua-5.3.6/src -Llua-5.3.6/src -llua53

mingw51 :
	gcc -g -Wall --shared -o snapshot.dll snapshot.c -Ilua-5.1.5/src -Llua-5.1.5/src -llua51

macosx :
	gcc -g -Wall --shared -undefined dynamic_lookup -o snapshot.so snapshot.c

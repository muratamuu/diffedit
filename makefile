diffedit: diffedit.o
	g++ -o diffedit diffedit.o

diffedit.o: diffedit.cxx
	g++ -O2 -c diffedit.cxx

clean:
	\rm diffedit diffedit.o ~*

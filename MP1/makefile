all: server client

server: crsd.cpp interface.h
	g++ crsd.cpp -o server -O1 -pthread

client: crc.cpp interface.h
	g++ crc.cpp -o client -O1 -pthread

clean:
	rm server client *.o

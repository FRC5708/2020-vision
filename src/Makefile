
COMMON_FLAGS=-pg -Og -Wno-psabi -Wno-sign-compare -fopenmp -march=native -mcpu=native -mtune=native
CXXFLAGS=-g -Wall --std=c++17 -ffast-math $(COMMON_FLAGS) -I/usr/include/opencv4/


ODIR=obj

%.o: %.cpp %.hpp
	g++ -c -o $@ $< $(CXXFLAGS)

OBJS=main.o RedContourGrip.o vision.o streamer.o DataComm.o VideoHandler.o ControlPacketReceiver.o

build: $(OBJS)
	g++ -Wno-psabi $(COMMON_FLAGS) -o ../5708-vision $(OBJS) -lm -ldl `pkg-config --libs opencv4` -pthread

install:
	cp ../5708-vision ../5708-vision-copy
	mv ../5708-vision-copy ~/bin/5708-vision

clean:
	rm ./*.o

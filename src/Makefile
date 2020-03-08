
COMMON_FLAGS=-pg -Og -Wno-psabi -fopenmp -march=native -mcpu=native -mtune=native
# DO NOT enable -ffast-math! it breaks isnan()
CXXFLAGS=-ggdb -Wall --std=c++17 $(COMMON_FLAGS) -I/usr/include/opencv4/


ODIR=obj

%.o: %.cpp %.hpp
	g++ -c -o $@ $< $(CXXFLAGS)

OBJS=main.o vision.o streamer.o DataComm.o VideoHandler.o ControlPacketReceiver.o GripHexFinder.o

build: $(OBJS)
	g++ $(COMMON_FLAGS) -o ../5708-vision $(OBJS) -lm -ldl `pkg-config --libs opencv4` -pthread

install:
	cp ../5708-vision ../5708-vision-copy
	mv ../5708-vision-copy ~/bin/5708-vision

clean:
	rm ./*.o

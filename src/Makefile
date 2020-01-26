
COMMON_FLAGS=-Wno-psabi -fopenmp -march=native -mcpu=native -mtune=native
CXXFLAGS=-g -Wall --std=c++17 -ffast-math $(COMMON_FLAGS) -I/usr/include/opencv4/
CFLAGS=-g -Wall $(COMMON_FLAGS) -I/opt/vc/include/ -I/usr/include/opencv4/

export PKG_CONFIG_PATH := $(PKG_CONFIG_PATH):/opt/vc/lib/pkgconfig


ODIR=obj

%.o: %.cpp %.hpp
	g++ -c -o $@ $< $(CXXFLAGS)

jpeg.o: jpeg.c
	gcc -c -o $@ $< $(CFLAGS)

OBJS=main.o RedContourGrip.o vision.o streamer.o DataComm.o VideoHandler.o jpeg.o jpeg-wrapper.o

build: $(OBJS)
	g++ -Wno-psabi $(COMMON_FLAGS) -o ../5708-vision $(OBJS) -lm -ldl `pkg-config --libs opencv4 mmal` -pthread

install:
	cp ../5708-vision ../5708-vision-copy
	mv ../5708-vision-copy ~/bin/5708-vision

clean:
	rm ./*.o

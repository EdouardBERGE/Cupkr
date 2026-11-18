all: upkr

upkr: main.cpp upkr.cpp
	g++ -std=c++17 -O2 -o upkr main.cpp upkr.cpp

clean:
	rm -f upkr

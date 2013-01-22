all: chess.so chess_runner

chess.so: chess.cpp
	g++ -o $@ -Wall -shared -g -O0 -D_GNU_SOURCE -fPIC -ldl $^

chess_runner: chess_runner.cpp
	g++ -o $@ -g $^

This is a small tool is an implementation of the CHESS algorithm for testing multithreaded applications for deadlocks. This current implementation is restricting to testing applications with only 2 threads. It works by exploring the execution of a program and marking points where there is a possibility of a context switch. It then reruns the program multiple times and forcing a context switch at a different point in each execution. This results in executing every possible interleaving of thread context switches. If the program is prone to deadlocks, one of the executions will find it. This helps immensely in multithreaded debugging. More information about the algorithm used can be found here: ftp://ftp.research.microsoft.com/pub/tr/TR-2007-149.pdf.

Typing 'make' should compile the tool. If not, type 'g++ -o chess_runner chess_runner.cpp'.

To run the tool, type 'chess_runner <prog> [args]', where <prog> is the program you wish to test and
[args] are a list of arguments to pass to <prog> (optional).

chess.so and run.sh are required for the tool to work. Also, you may have to give run.sh execute permissions.

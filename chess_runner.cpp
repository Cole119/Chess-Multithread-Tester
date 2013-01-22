#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This is used to run the given program with
 * the chess tool.
 */
int main(int argc, char **argv){
	char command[128];
	FILE * file;
	int num_exec = 0;
	int n;

	if(argc < 2){
		printf("Usage: chess_runner <program> <args>\n");
		exit(0);
	}

	//remove the last chess_sequence file if it exists
	//so that the chess tool will know we want to explore.
	remove("chess_sequence");

	//run.sh is used to run the given program with the chess
	//tool set as the thread library
	sprintf(command, "run.sh");
	for(int i=1;i<argc;i++){
		strcat(command, " ");
		strcat(command, argv[i]);
	}
	strcat(command, " > chess_runner.out");

	printf("Exploring...\n\n");
	system(command);

	file = fopen("chess_sequence", "r+");
	if(!file){
		fprintf(stderr, "ERROR: Couldn't open sequence file.\n");
		exit(1);
	}

	fscanf(file, "%d", &num_exec);

	//now we run the given program num_exec times. Each time we
	//execute the program, we increment the int saved in the
	//file so that chess knows when to context switch.
	//If the tool encounters a deadlock, execution will freeze
	//and can be interrupted with CTRL-C. This runner will
	//then print out which execution it froze so that you will 
	//know exactly how to generate the deadlock again consistently.
	for(n=0; n<num_exec; n++){
		printf("Executing step %d/%d\n", n, num_exec-1);
		fseek(file, 0, SEEK_SET);
		fprintf(file, "%d", n);
		int ret = system(command);
		if(ret != 0){
			printf("\nInterrupted at n = %d\n", n);
			break;
		}
	}

	printf("\nDone executing.\n");
	fclose(file);

	return 0;
}
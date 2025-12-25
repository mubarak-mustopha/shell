#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void print_shell_prompt(bool shell_is_interactive, int* line_num){
	if (shell_is_interactive)
		fprintf(stdout, "%d: ", ++(*line_num));
}

void find_program_path(char* program, char* program_path){
        // prog = "/program"
	assert(program != NULL);
	assert(program_path != NULL);

        char slash_prog[strlen(program) + 2];
        strcpy(slash_prog, "/");
        strcat(slash_prog, program);

        char* PATHENV = strdup(getenv("PATH"));
        char *str, *path, *save_ptr;
        for (str=PATHENV;  ; str=NULL){
                path = strtok_r(str, ":", &save_ptr);
                if (path == NULL)
                        break;

                strcpy(program_path, path);
                strcat(program_path, slash_prog);
                if (access(program_path, X_OK) == 0){
			free(PATHENV);
		     	return;
                }
        }

        /* path not found */
        program_path[0] = '\0';
	free(PATHENV);
}

int close_unused_pipe_fds(int (*pipe_arr)[2], int arr_size, int proc_ind){
	/* 
	 * Example pipeline:
	 * 	prop0 | proc1 | proc2 | ...
	 *
	 * Rules
	 * - pipe0: write end used by proc0, read end used by proc1
	 * - pipe1: write end used by proc1, read end used by proc2
	 * - In general, pipe i connects proc i -> proc (i + 1):
	 *   	* write fd belongs to proc i
	 *   	* read fd belongs to proc (i + 1)
	 * */
	
	int i, j, rc;
	for (i = 0; i < arr_size; i++){
		for (j = 0; j < 2; j++){
			if ( (i == proc_ind && j == 1) || (proc_ind == i + 1 && j == 0) )
				continue;
			rc = close(pipe_arr[i][j]);
			if (rc < 0)
				return -1;
		}
	}

	return 0;
}

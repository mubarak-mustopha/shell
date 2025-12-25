#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"
#include "utils.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);
int cmd_pwd(struct tokens* tokens);
int cmd_cd(struct tokens* tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens* tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t* fun;
  char* cmd;
  char* doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_pwd, "pwd", "prints the current working directory"},
    {cmd_cd, "cd", "changes the current working directory"},
};

/* Changes the current working directory */
int cmd_cd(struct tokens* tokens){
	assert(tokens != NULL);
	if (tokens->tokens_length != 2){
		puts("cd takes exactly one argument");
	}else {
		if (chdir(tokens->tokens[1]))
			fprintf(stderr, "cd: %s: %s\n", tokens->tokens[1],strerror(errno));
		else
			return 1;
	}
	return -1;
}

/* Prints the current working directory */
int cmd_pwd(unused struct tokens* tokens){
	char cwd[1024];
	if (getcwd(cwd, 1024)){
		puts(cwd); return 1;
	}
	else{
		fprintf(stderr, "pwd: %s", strerror(errno));
		return -1;
	}
}

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens* tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens* tokens) { exit(0); }

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}


int main(unused int argc, unused char* argv[]) {
  init_shell();

  /* signals */
  struct sigaction sa;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = SIG_IGN;
  sigaction(SIGTTOU, &sa, NULL);  
  sigaction(SIGTSTP, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);  

  static char line[4096];
  int line_num = 0;
	
  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens* tokens = tokenize(line);
	
    if (tokens_get_length(tokens) == 0) {
    	print_shell_prompt(shell_is_interactive, &line_num);
    	tokens_destroy(tokens);
	continue;
    }
    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* REPLACE this to run commands as programs. */
	bool bg = false;
	if (strcmp(tokens_get_token(tokens, tokens->tokens_length - 1), "&") == 0)
		bg = true;
		
	/* there cannot be more commands than tokens_length */
	int *cmd_start_indexes = malloc(sizeof(int) * tokens->tokens_length);
	cmd_start_indexes[0] = 0;

	/* figure out the number of processes to run */
	int nprocs = 1;
	for (int i=0; i < tokens->tokens_length; i++){
		/* n pipes i.e `|` indicates `n + 1` processes */
		if (strcmp(tokens_get_token(tokens, i), "|") == 0){
			cmd_start_indexes[nprocs] = i + 1;
			nprocs++;
		}
	}

	/* fork child processes */
	int cpid;
        int child_pgid;	
	int child_idx; // index of child process, esp useful for commands invloving pipes

	if (nprocs > 1) {
		/* create pipes for inter-process communication */
		int (*pipe_arr)[2] = malloc(sizeof(int[2]) * (nprocs - 1));
		int rc = 0;
		for (int i = 0; i < nprocs - 1; i++){
			rc = pipe(pipe_arr[i]);
			if (rc < 0){
				fprintf(stderr, "pipe: %s", strerror(errno));
				break;
			}	
		}
		
		if (rc < 0)
			continue;

		/* create child processes  */
		for (int i = 0; i < nprocs; i++){
			
			cpid = fork();
			if (cpid < 0){
				fprintf(stderr, "fork: %s\n", strerror(errno));
				break;
			}

			if (cpid == 0){	
				sa.sa_handler = SIG_DFL;
				sigaction(SIGTTOU, &sa, NULL);
				sigaction(SIGTSTP, &sa, NULL);
				sigaction(SIGINT, &sa, NULL);
				child_idx= i;
				if (i == 0){
					/* first process in pipeline */
					setpgrp();
					dup2(pipe_arr[i][1], STDOUT_FILENO);
					rc = close_unused_pipe_fds(pipe_arr, nprocs - 1, i);
				} else {
					setpgid(0, child_pgid);
					if (i == nprocs - 1){
						/* last process in pipeline */
						dup2(pipe_arr[i - 1][0], STDIN_FILENO);
						rc = close_unused_pipe_fds(pipe_arr, nprocs - 1, i);
					} else {
						dup2(pipe_arr[i - 1][0], STDIN_FILENO);
						dup2(pipe_arr[i][1], STDOUT_FILENO);
						rc = close_unused_pipe_fds(pipe_arr, nprocs - 1, i);
					}
				}
				if (rc < 0){
					fprintf(stderr, "Error closing fds: %s\n", strerror(errno));	
				}
				break;
			}else if (cpid > 0 && i == 0){
				child_pgid = cpid;
				/* set all children processes pgid to pid of first child */
				setpgid(cpid, child_pgid);
				/* set child_pgid to foreground */
				if (!bg)
					tcsetpgrp(shell_terminal, child_pgid);
			}
				
		}

		if (cpid > 0) {
			/* close all pipe fds for parent */
			for (int i = 0; i < nprocs - 1; i++){
				for (int j = 0; j < 2; j++)
					close(pipe_arr[i][j]);
			}
			free(pipe_arr);
		}
	    
	}else {
		child_idx= 0;
		cpid = fork();
		
		if (cpid > 0){
			child_pgid = cpid;
			setpgid(cpid, child_pgid);
			/* set child_pgid to foreground */
			if (!bg)
				tcsetpgrp(shell_terminal, child_pgid);
		} else if (cpid == 0){
			setpgrp();
			sa.sa_handler = SIG_DFL;
			sigaction(SIGTTOU, &sa, NULL);
			sigaction(SIGTSTP, &sa, NULL);
			sigaction(SIGINT, &sa, NULL);
		}
	}

	if (cpid > 0){
		// parent process	
		free(cmd_start_indexes);
		
		/* wait until last child process terminates */
		if (!bg){
			waitpid(cpid, NULL, 0);
			tcsetpgrp(shell_terminal, shell_pgid);
		}
	} else if (cpid == 0){
		
		char* program = tokens->tokens[cmd_start_indexes[child_idx]];
		int arg_c = 0;
		char** args = malloc(sizeof(char*) * (tokens->tokens_length + 1));
		/* check input/output redirection */
		for (int i = cmd_start_indexes[child_idx]; i < tokens->tokens_length;){
			if (strcmp(tokens->tokens[i], "<") == 0){
				int fd = open(tokens->tokens[i + 1], O_RDONLY);
				if (fd < 0){
					fprintf(stderr, "%s: %s\n", tokens->tokens[i + 1], strerror(errno));
					exit(1);
				}

				dup2(fd, STDIN_FILENO);
				i += 2;
			} else if (strcmp(tokens->tokens[i], ">") == 0){
				int fd = open(tokens->tokens[i + 1], O_CREAT|O_TRUNC|O_WRONLY, 0644);
				if (fd < 0){
					fprintf(stderr, "%s: %s\n", tokens->tokens[i + 1], strerror(errno));
					exit(1);
				}

				dup2(fd, STDOUT_FILENO);
				i += 2;
			} else if (strcmp(tokens->tokens[i], "|") == 0 || strcmp(tokens->tokens[i], "&") == 0){
				break;
			} else {
				args[arg_c] = tokens->tokens[i];
				arg_c++;i++;
			}
		}
		/* indicate end of argument with NULL as required by exec */
		args[arg_c] = NULL;
		
		if (access(program, X_OK) == 0){
			execv(program, args);
		} else {
			/* find program path */
			char program_path[1024];
			find_program_path(program, program_path);
			if (strlen(program_path) == 0){
				fprintf(stderr, "'%s': command not found\n", program);
				exit(1);
			}
			execv(program_path, args);

			fprintf(stderr, "'%s' command execution failed: %s\n", program_path, strerror(errno));
			
			exit(1);
		}

	} else {
		free(cmd_start_indexes);
		fprintf(stderr, "fork: %s\n", strerror(errno));
	}
    }

     /* Please only print shell prompts when standard input is not a tty */
    print_shell_prompt(shell_is_interactive, &line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}

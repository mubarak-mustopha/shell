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
int cmd_wait(struct tokens* tokens);

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
    {cmd_wait, "wait", "wait until all background jobs have terminated"},
};

int cmd_wait(struct tokens* tokens){
	if (tokens->tokens_length > 1){
		fprintf(stderr, "wait takes no arguemnt\n");
		return -1;
	}

	int rc;
	do {
		rc = wait(NULL);
	} while (rc > 0);
	return 1;
}

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

/* ===== REFACTORED HELPER FUNCTIONS ===== */

/**
 * Checks if the command should run in background (ends with &)
 */
bool is_background_job(struct tokens* tokens) {
    if (tokens->tokens_length == 0) {
        return false;
    }
    return strcmp(tokens_get_token(tokens, tokens->tokens_length - 1), "&") == 0;
}

/**
 * Analyzes tokens to find pipe positions and command boundaries
 * Returns the number of commands in the pipeline
 */
int parse_pipeline(struct tokens* tokens, int* command_start_positions) {
    command_start_positions[0] = 0;
    int num_commands = 1;
    
    for (int i = 0; i < tokens->tokens_length; i++) {
        if (strcmp(tokens_get_token(tokens, i), "|") == 0) {
            command_start_positions[num_commands] = i + 1;
            num_commands++;
        }
    }
    
    return num_commands;
}

/**
 * Creates pipes for inter-process communication
 * Returns 0 on success, -1 on failure
 */
int create_pipeline_pipes(int num_commands, int (**pipe_fds)[2]) {
    int num_pipes = num_commands - 1;
    *pipe_fds = malloc(sizeof(int[2]) * num_pipes);
    
    for (int i = 0; i < num_pipes; i++) {
        if (pipe((*pipe_fds)[i]) < 0) {
            fprintf(stderr, "pipe: %s\n", strerror(errno));
            free(*pipe_fds);
            return -1;
        }
    }
    
    return 0;
}

/**
 * Sets up pipe redirection for a child process in a pipeline
 */
void setup_pipe_redirection(int process_index, int num_commands, int (*pipe_fds)[2]) {
    // Not first process: read from previous pipe
    if (process_index > 0) {
        dup2(pipe_fds[process_index - 1][0], STDIN_FILENO);
    }
    
    // Not last process: write to current pipe
    if (process_index < num_commands - 1) {
        dup2(pipe_fds[process_index][1], STDOUT_FILENO);
    }
}

/**
 * Configures signal handlers for child processes (restore defaults)
 */
void setup_child_signal_handlers() {
    struct sigaction sa;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_DFL;
    
    sigaction(SIGTTOU, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
}

/**
 * Sets up process group for child process
 */
void setup_child_process_group(int process_index, pid_t first_child_pid) {
    pid_t target_pgid = (process_index == 0) ? 0 : first_child_pid;
    setpgid(0, target_pgid);
}

/**
 * Handles file redirection (< and >) and builds argument array for exec
 * Returns the argument count
 */
int parse_redirections_and_args(struct tokens* tokens, int command_start, 
                                char** args_array) {
    int arg_count = 0;
    
    for (int i = command_start; i < tokens->tokens_length; ) {
        char* token = tokens->tokens[i];
        
        // Input redirection
        if (strcmp(token, "<") == 0) {
            char* input_file = tokens->tokens[i + 1];
            int fd = open(input_file, O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "%s: %s\n", input_file, strerror(errno));
                exit(1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
            i += 2;
        }
        // Output redirection
        else if (strcmp(token, ">") == 0) {
            char* output_file = tokens->tokens[i + 1];
            int fd = open(output_file, O_CREAT | O_TRUNC | O_WRONLY, 0644);
            if (fd < 0) {
                fprintf(stderr, "%s: %s\n", output_file, strerror(errno));
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            i += 2;
        }
        // End of this command in pipeline
        else if (strcmp(token, "|") == 0 || strcmp(token, "&") == 0) {
            break;
        }
        // Regular argument
        else {
            args_array[arg_count++] = token;
            i++;
        }
    }
    
    // NULL-terminate the argument array as required by exec
    args_array[arg_count] = NULL;
    return arg_count;
}

/**
 * Executes a program with given arguments
 * Tries direct execution first, then searches PATH
 */
void execute_program(char* program_name, char** args) {
    // Try direct execution if program is executable
    if (access(program_name, X_OK) == 0) {
        execv(program_name, args);
        fprintf(stderr, "'%s' execution failed: %s\n", program_name, strerror(errno));
        exit(1);
    }
    
    // Search for program in PATH
    char program_path[1024];
    find_program_path(program_name, program_path);
    
    if (strlen(program_path) == 0) {
        fprintf(stderr, "'%s': command not found\n", program_name);
        exit(1);
    }
    
    execv(program_path, args);
    fprintf(stderr, "'%s' execution failed: %s\n", program_path, strerror(errno));
    exit(1);
}

/**
 * Forks and executes a single command in a pipeline
 */
pid_t fork_and_execute_command(struct tokens* tokens, int process_index, 
                               int num_commands, int* command_starts,
                               int (*pipe_fds)[2], pid_t first_child_pid) {
    pid_t child_pid = fork();
    
    if (child_pid < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return child_pid;
    }
    
    // Parent process
    if (child_pid > 0) {
        return child_pid;
    }
    
    // Child process
    setup_child_signal_handlers();
    setup_child_process_group(process_index, first_child_pid);
    
    // Set up pipe redirection if in a pipeline
    if (num_commands > 1) {
        setup_pipe_redirection(process_index, num_commands, pipe_fds);
        if (close_unused_pipe_fds(pipe_fds, num_commands - 1, process_index) < 0) {
            perror("child pipe close");
            exit(1);
        }
    }
    
    // Parse redirections and build argument list
    char* program_name = tokens->tokens[command_starts[process_index]];
    char** args = malloc(sizeof(char*) * (tokens->tokens_length + 1));
    parse_redirections_and_args(tokens, command_starts[process_index], args);
    
    // Execute the program
    execute_program(program_name, args);
    
    // Never reached if exec succeeds
    exit(1);
}

/**
 * Executes a pipeline of commands
 */
void execute_pipeline(struct tokens* tokens, int num_commands, 
                     int* command_starts, bool run_in_background) {
    int (*pipe_fds)[2] = NULL;
    
    // Create pipes if needed
    if (num_commands > 1) {
        if (create_pipeline_pipes(num_commands, &pipe_fds) < 0) {
            return;
        }
    }
    
    pid_t first_child_pid = 0;
    pid_t last_child_pid = 0;
    
    // Fork and execute each command in the pipeline
    for (int i = 0; i < num_commands; i++) {
        pid_t child_pid = fork_and_execute_command(tokens, i, num_commands, 
                                                   command_starts, pipe_fds, 
                                                   first_child_pid);
        
        if (child_pid < 0) {
            if (pipe_fds) free(pipe_fds);
            return;
        }
        
        // Parent process: manage process groups
        if (i == 0) {
            first_child_pid = child_pid;
            setpgid(child_pid, first_child_pid);
            
            // Set to foreground if not a background job
            if (!run_in_background) {
                tcsetpgrp(shell_terminal, first_child_pid);
            }
        }
        
        last_child_pid = child_pid;
    }
    
    // Parent: close all pipe file descriptors
    if (pipe_fds) {
        close_unused_pipe_fds(pipe_fds, num_commands - 1, -1);
        free(pipe_fds);
    }
    
    // Wait for pipeline to complete (unless background job)
    if (!run_in_background) {
        waitpid(last_child_pid, NULL, WUNTRACED);
        tcsetpgrp(shell_terminal, shell_pgid);
    }
}

/**
 * Handles execution of non-built-in commands
 */
void execute_external_command(struct tokens* tokens) {
    bool run_in_background = is_background_job(tokens);
    
    // Allocate array to track where each command starts
    int* command_starts = malloc(sizeof(int) * tokens->tokens_length);
    if (!command_starts) {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }
    
    // Parse the pipeline to find command boundaries
    int num_commands = parse_pipeline(tokens, command_starts);
    
    // Execute the pipeline
    execute_pipeline(tokens, num_commands, command_starts, run_in_background);
    
    free(command_starts);
}

/**
 * Processes a single command line
 */
void process_command_line(struct tokens* tokens) {
    // Empty command
    if (tokens_get_length(tokens) == 0) {
        return;
    }
    
    // Check if it's a built-in command
    int builtin_index = lookup(tokens_get_token(tokens, 0));
    
    if (builtin_index >= 0) {
        cmd_table[builtin_index].fun(tokens);
    } else {
        execute_external_command(tokens);
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
	
    /* Process the command */
	process_command_line(tokens);

     /* Please only print shell prompts when standard input is not a tty */
    print_shell_prompt(shell_is_interactive, &line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}

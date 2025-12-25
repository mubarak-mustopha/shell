void print_shell_prompt(bool shell_is_interactive, int* line_num);

void find_program_path(char* program, char* program_path);

int close_unused_pipe_fds(int (*pipe_arr)[2], int arr_size, int proc_ind);

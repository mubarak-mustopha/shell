#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void find_program_path(char* program, char* program_path){
        // prog = "/program"
        char slash_prog[strlen(program) + 2];
        strcpy(slash_prog, "/");
        strcat(slash_prog, program);

        char* PATHENV = getenv("PATH");
        char *str, *path, *save_ptr;
        for (str=PATHENV;  ; str=NULL){
                path = strtok_r(str, ":", &save_ptr);
                if (path == NULL)
                        break;

                strcpy(program_path, path);
                strcat(program_path, slash_prog);
                if (access(program_path, X_OK) == 0){
                        return;
                }
        }

        /* path not found */
        program_path[0] = '\0';
}

void exec_program(char* pathname, int argc, char* argv[]){
        char* args[argc + 1];
        for (int i=0; i < argc; i++)
                args[i] = argv[i];
        args[argc] = NULL;
        execv(pathname, args);
}


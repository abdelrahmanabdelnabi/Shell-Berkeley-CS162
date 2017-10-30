#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>

#include "tokenizer.h"
typedef struct err_map_node{/*a struct to represent an error number and its meaning*/
    int number;
    char* desc;
}err_map_node_t;
/*a list of errors returned by cd and their corresponding meaning*/
err_map_node_t errs_map[]= {
        {EACCES,"Search permission is denied for one of the components  of  path."},
        {EFAULT,"path points outside your accessible address space."},
        {EIO    ,"An I/O error occurred."},
        {ELOOP ,"Too many symbolic links were encountered in resolving path."},
        {ENAMETOOLONG,"path is too long."},
        {ENOENT, "The file does not exist."},
        {ENOMEM, "Insufficient kernel memory was available."},
        {ENOTDIR,"A component of path is not a directory."}
};
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

/* The number of processes running in the background */
int nr_background;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
    cmd_fun_t *fun;
    char *cmd;
    char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
        {cmd_help, "?", "show this help menu"},
        {cmd_exit, "exit", "exit the command shell"},
        {cmd_pwd, "pwd", "print the current working directory"},
        {cmd_cd, "cd", "change the current directory to the directory given as an argument"}
};



/*looks up the description of the error number passed as argument*/
char *lookup_errno(int number)
{
    for(unsigned int i = 0;i< sizeof(errs_map)/ sizeof(err_map_node_t);i++)
        if(errs_map[i].number == number)
            return errs_map[i].desc;

    return NULL;
}
/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
    for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
        printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
    return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
    exit(0);
}

int cmd_pwd(unused struct tokens *tokens){
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    fprintf(stdout, "%s\n",cwd);
    return 1;
}

/*sets the current directory*/
int cmd_cd(struct tokens *tokens){

    int result = chdir(tokens_get_token(tokens, 1));
    if(result == 0)
        return 1;
    else {
        if(lookup_errno(errno))
            fprintf(stdout, "%s\n",lookup_errno(errno));
        else
            fprintf(stdout,"an unknown error occurred");
        return 0;
    }
}
/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
    for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
        if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
            return i;
    return -1;
}

char *join_paths(char *path1,char *path2){

    char *joined_path = (char*)malloc(4096);
    strcpy(joined_path,path1);
    strcat(joined_path,"/");
    strcat(joined_path,path2);
    joined_path[strlen(path1)+strlen(path2)+1]=0;
    return joined_path;
}

/*checks if file_name exists within directory dir_name*/
int file_exists_in_dir(char *dir_name,char *file_name){

    int ret = 0;
    DIR *dir_ptr;
    struct dirent *dir;
    dir_ptr = opendir(dir_name);
    if (dir_ptr)
    {
        while ((dir = readdir(dir_ptr)) != NULL)
        {
            if(dir->d_type == DT_DIR /*if this is a directory*/
               && strcmp(dir->d_name,"..")!=0 /*and is not a parent directory reference*/
               && strcmp(dir->d_name, ".")!=0 ){/*and is not a self reference*/

                /*try to find the file in this sub directory*/
                ret |= file_exists_in_dir(join_paths(dir_name,dir->d_name),file_name);
            }

            /*if this is a file and has the same name as file_name */
            if((dir->d_type == DT_REG) && strcmp(dir->d_name, file_name) == 0) {
                ret = 1;
                break;
            }
        }
        closedir(dir_ptr);
    }
    return ret;
}

/*resolves the program from the path environment variable*/
char *get_resolved_path(char* prog_name){
    int found=0;
    char *path_env_var = getenv("PATH");
    struct tokens *tokens = tokenize(path_env_var,":");
    char *parent_dir = (char*)malloc(4096);
    for(unsigned int i = 0;i<tokens_get_length(tokens);i++){
        /*if this path has the requested file*/
       if(file_exists_in_dir(tokens_get_token(tokens,i),prog_name)){
           /*this is the directory we are looking for*/
           strcpy(parent_dir,tokens_get_token(tokens,i));
           found = 1;
       }
    }
    if(found)
        return join_paths(parent_dir,prog_name);
    else
        return NULL;
}


/* Intialization procedures for this shell */
void init_shell() {
    /* Our shell is connected to standard input. */
    shell_terminal = STDIN_FILENO;

    /* Check if we are running interactively */
    shell_is_interactive = isatty(shell_terminal);

    nr_background = 0;

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


char** get_args(struct tokens *tokens) {
    int length = tokens_get_length(tokens);
    if(length == 0)
        return NULL;
    
    char **args = malloc((length + 1 ) * sizeof(char*)); // +1 to terminate the array with null
    int index = 0;
    for(int i = 0; i < length; i++) {
        // TODO: only pass arguements and keep pipes and "&" for the shell to interpret
        char* token = tokens_get_token(tokens, i);
        if(strncmp(token, "&", strlen("&")) != 0) {
            args[index] = token;
            index++;
        }
    }

    // terminate the args array
    args[index] = 0;
    return args;
}

int isBackground(struct tokens *tokens) {
    int length = tokens_get_length(tokens);
    if(length == 0)
        return 0;
    char* last = tokens_get_token(tokens, length - 1);
    if(strncmp(last, "&", strlen("&")) == 0)
        return 1;
    return 0;
}

void child_exit_handler(int sig) {
    wait(NULL);

    nr_background--;
}

int main(unused int argc, unused char *argv[]) {
    init_shell();
    char *SPACE_CHARS = " \f\r\t\v\n";
    static char line[4096];
    int line_num = 0;
    // signal(SIGCHLD,child_exit_handler);

    /* Please only print shell prompts when standard input is not a tty */
    if (shell_is_interactive)
        fprintf(stdout, "%d: ", line_num);

    while (fgets(line, 4096, stdin)) {
        /* Split our line into words. */
        struct tokens *tokens = tokenize(line,SPACE_CHARS);

        /* Find which built-in function to run. */
        int fundex = lookup(tokens_get_token(tokens, 0));

        if (shell_is_interactive)
            /* Please only print shell prompts when standard input is not a tty */
            fprintf(stdout, "%d: ", ++line_num);

        if (fundex >= 0) {
            cmd_table[fundex].fun(tokens);
        } else {
            /* REPLACE this to run commands as programs. */
            char *cmd = tokens_get_token(tokens, 0);
            char **args = get_args(tokens);
            char *resolved_path = get_resolved_path(cmd); 

            if( resolved_path == NULL) {
                if (shell_is_interactive)
                    /* Please only print shell prompts when standard input is not a tty */
                    fprintf(stdout, "%d: ", ++line_num);
                continue;
            }

            int background = isBackground(tokens);
            if(background)
                nr_background++;
            
            pid_t child = fork();

            if(child == 0) {  // child process
                if(execv(resolved_path, args) == -1) {
                    printf("error occurred executing command\n");
                    printf("error message: %s\n", strerror(errno));
                    exit(0);
                }
            } else { // parent
                if(!background)
                    waitpid(child, NULL, 0);
            }
            free(args);

            /* Clean up memory */
            tokens_destroy(tokens);
        }
    }

    return 0;
}


/*TODO factor join paths into a util file*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

// struct for easy mapping between the build-in cmds 
typedef struct {
    char *name;
    int (*func)(char **);
} BuiltIns;

// struct for history utility
typedef struct {
    char **recent_cmds;
    int size;
    int count;
    int startIdx;
} History;  

// struct for shell vars
typedef struct {
    char *keys[100];
    char *values[100];
    int count; // for printing
} ShellVars;

ShellVars shellVars = { .count = 0 }; 

void main_rsh(void);
char* read_input(void);
char** parse_input(char*);
int execute_input(char**);
void execute_pipes(char**, int);
int batch(char*);

// built-in commands
int rsh_cd(char **args);
int rsh_exit(char **args);
int rsh_export(char **args);
int rsh_local(char **args);
int rsh_vars(char** args);
int rsh_history(char** args);
void init_history();
void update_history(char*);

// map built-in arg to function
BuiltIns builtin_cmds[] = {
    {"history", rsh_history},
    {"cd", rsh_cd},
    {"exit", rsh_exit},
    {"export", rsh_export},
    {"local", rsh_local},
    {"vars", rsh_vars}
};

#define num_builtins 6
#define history_size_init 5

History history = {NULL, history_size_init, 0, 0}; // init history struct

int main(int argc, char **argv) {
    // prevent program from quitting when ctrl+c is pressed  
    signal(SIGINT, SIG_IGN); 
    init_history();
    // assume batch mode if 2 args passed when calling rsh 
    if (argc == 2) { batch(argv[1]); }
    else { main_rsh(); }
    return 0;
}

int is_builtin_command(char* cmd) {
    for (int i = 0; i < num_builtins; i++) {
        if (strcmp(cmd, builtin_cmds[i].name) == 0) {
            return 1; // built-in
        }
    }
    return 0; // not built-in
}

// main rsh loop
void main_rsh(void) {
    do {
        printf("rsh> ");
        char* input = read_input();
        char* inputCpy = strdup(input);
        char** argv = parse_input(input);
        execute_input(argv); 
        // don't add built-ins to history 
        if (!is_builtin_command(argv[0]) && history.size != 0) {
            update_history(inputCpy);
        }
        free(input);
        free(argv);
    } while (1);
}

// batch mode 
int batch(char* batchFP) {
    FILE* file = fopen(batchFP, "r");
    if (file == NULL) {
        printf("Error opening batch file -  %s\n", batchFP);
        exit(1);
    }

    char *line = NULL;
    size_t buf_size = 0;
    ssize_t charCount; // getline() can return -1
    // ideally we would pass each line into the main rsh loop
    // however it takes in no arguments and it's just easier
    // to parse the input here and directly execute it

    // loop through each line
    while ((charCount = getline(&line, &buf_size, file)) != -1) {
        // replace new line char with null terminator: '\n' -> '\0'
        if (line[charCount - 1] == '\n') line[charCount - 1] = '\0';
        char **args = parse_input(line);
        execute_input(args);
        free(args);
    }

    free(line);
    fclose(file);
    return 0;
}

// read input from stdin 
char* read_input(void) {
    char *line = NULL;
    size_t bufsize = 0; // buffer size for line; auto updated by getline
    if (getline(&line, &bufsize, stdin) == -1) {
        if (feof(stdin)) {
            exit(0); // ctrl+d
        } else {
            printf("Error reading input\n"); 
            exit(1);
        }
    }
    return line; 
}

// map var.key to var.val
char* variable_sub(char* var_name) {
    // could be env var
    char* value = getenv(var_name);
    if (value) { return value; }

    // First check in local variables
    for (int i = 0; i < shellVars.count; i++) {
        if (strcmp(shellVars.keys[i], var_name) == 0) {
            return shellVars.values[i];
        }
    }
    return NULL; // no match
}

// break up input for easy execution
char** parse_input(char* input) {
    int bufsize = 64, pIndex = 0; 
    // arguments: array of char*
    char **args = malloc(bufsize * sizeof(char*)); // TODO: free

    if (!args) {
        printf("Error allocating args\n");  
        exit(1);
    }

    char *arg; // individual argument, element of args
    // first arg is retrieved through seperation of input 
    arg = strtok(input, " \t\r\n\a");
    // loop through input and break it into words through a delimeter 
    while (arg != NULL) {
        // set the pIndex-th element in the args array to be the retrieved element
        if (arg[0] == '$') { // check if var
            char* var_name = arg + 1; // skip '$'
            char* var_value = variable_sub(var_name);
            if (var_value && strlen(var_value) > 0) { // If variable exists, add its value to args
                args[pIndex++] = strdup(var_value);
            } 
            // free(args[i]); 
        } else { 
            args[pIndex++] = arg;
        }

        // if our buffer is not big enough, reallocate
        // this is a best practice instead of allocating a giant buffer
        if (pIndex >= bufsize) {
            bufsize += 64;
            args = realloc(args, bufsize * sizeof(char*));
            if (!args) {
                printf("Error reallocating args\n");
                exit(1);
            }
        }
        // continue tokenizing from where we left off
        arg = strtok(NULL, " \t\r\n\a");
    }
    // NULL terminate the array
    args[pIndex] = NULL;

    return args;
}

// executes every arg passed
// fork arg into 2 processes -> use exec() on child
int execute_input(char** args) {
    // built-in command check
    if (args[0] == NULL) {
    printf("Empty input!\n");
    return (1);
    }

    // detect if we have any piping
    int pipe_count = 0;
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            pipe_count++;
        }
    }
    if (pipe_count > 0) {
        execute_pipes(args, pipe_count);
    } else {
        // check for built-in
        for (int i = 0; i < num_builtins; i++) {
            if (strcmp(args[0], builtin_cmds[i].name) == 0) {
                return builtin_cmds[i].func(args);
            }
        }

        // not built-in command; execute
        int wStatus;

        __pid_t pid = fork();
        if (pid==0) { 
            // child process successfully created
            if (execvp(args[0], args) == -1) {
                printf("execvp: No such file or directory\n");
                exit(1);
                } 
            }
        else if (pid < 0) {
            // error with fork()
            printf("Error forking process\n");
            exit(1);
        } 
        else {
            // parent case: need to wait for child
            do {
                waitpid(pid, &wStatus, WUNTRACED); // return if child has stopped
            // processes didn't exit or return a signal
            } while (!WIFEXITED(wStatus) && !WIFSIGNALED(wStatus)); 
        }
    }
    return 0; // success!
}

// piping function
void execute_pipes(char **args, int num_pipes) {
    // need 2 file-descriptors per pipe (R/W)
    int pipe_fds[2 * num_pipes]; 
    pid_t pid;

    // create all pipes (map file descriptors)
    for (int i = 0; i < num_pipes; i++) {
        if (pipe(pipe_fds + i * 2) < 0) {
            printf("Error piping in execute_pipes");
            exit(1);
        }
    }

    int argCount = 0, argIdx = 0; 
    while (args[argCount] != NULL && argCount <= num_pipes) {
        // parsing: retrieve command and its arguments from input till 
        // we hit a pipe symbol
        char *arg[256];
        int k = 0;
        while (strcmp(args[argIdx], "|") != 0) {
            arg[k++] = args[argIdx++];
            if (args[argIdx] == NULL) { break; } // EoF
        }
        arg[k] = NULL; // null terminator for safety
        argIdx++; // skip '|'

        // new process for each command
        pid = fork();
        if (pid == 0) {
            // since not first, grab output from prev 
            if (argCount != 0) {
                if (dup2(pipe_fds[(argCount - 1) * 2], 0) < 0) {
                    printf("error with dup2 when piping"); 
                    exit(1);
                }
            }

            // vice versa; not last so push to next
            if (argCount < num_pipes) {
                if (dup2(pipe_fds[argCount * 2 + 1], 1) < 0) {
                    perror("error with dup2 when piping"); 
                    exit(1);
                }
            }

            // close all file-descriptors within a process (child!)
            for (int i = 0; i < 2 * num_pipes; i++) {
                close(pipe_fds[i]);
            }

            // execute command:
            // execvp expects name of program as first argument
            if (execvp(arg[0], arg) < 0) {
                printf("Error executing commend %s", arg[0]); // print eror as is (e.g not found)
                exit(1);
            }
        }
        argCount++;
    }

    // close all parent file-descriptors
    for (int i = 0; i < 2 * num_pipes; i++) {
        close(pipe_fds[i]);
    }

    // wait for children processes to exit
    while (num_pipes + 1 > 0) {
        wait(NULL); // 
        num_pipes--;
    }
}

// built-ins
int rsh_exit(char **args)
{
    exit(0); // terminate
}

int rsh_cd(char **args)
{
  if (args[1] == NULL) {
    printf("rsh: expected argument to \"cd\"\n");
  } else {
    if (chdir(args[1]) != 0) {
      printf("Error while using cd!\n");
    }
  }
  return 1;
}

int rsh_export(char **args) {
    if (args[1] == NULL) {
        printf("export: expected argument in the form VAR=value\n");
        return 1;
    }
    char *name = strtok(args[1], "=");
    char *value = strtok(NULL, "");
    
    if (name == NULL) {
        printf("export: invalid format. Use VAR=value\n");
        return 1;
    }

    // if a= : clear
    if (value == NULL) {
        unsetenv(name);
    } else { setenv(name, value, 1); }

    return 1;
}

int rsh_local(char **args) {
    if (args[1] == NULL) {
        printf("local: expected argument in the form VAR=value\n");
        return 1;
    }
    char *key = strtok(args[1], "=");
    char *value = strtok(NULL, "=");
    
    if (key == NULL) {
        printf("local: invalid format. Use VAR=value\n");
        return 1;
    }

    // clear through a= :
    if (value == NULL) {
        for (int i = 0; i < shellVars.count; i++) {
            if (strcmp(shellVars.keys[i], key) == 0) {
                free(shellVars.values[i]);
                free(shellVars.keys[i]);
                // shift everything
                for (int j = i; j < shellVars.count - 1; j++) {
                    shellVars.keys[j] = shellVars.keys[j + 1];
                    shellVars.values[j] = shellVars.values[j + 1];
                }
                shellVars.count--;
                return 1;
            }
        }
    }

    // check for duplicate; overwrite
    for (int i = 0; i < shellVars.count; i++) {
        if (strcmp(shellVars.keys[i], key) == 0) {
            free(shellVars.values[i]);
            shellVars.values[i] = strdup(value);
            return 1;
        }
    }

    if (shellVars.count == 100) {
        fprintf(stderr, "local: maximum number of variables reached\n");
        return 1;
    }
    // insert
    shellVars.keys[shellVars.count] = strdup(key);
    shellVars.values[shellVars.count] = strdup(value);
    shellVars.count++;

    return 1;
}

// print vars to stdout
int rsh_vars(char** args) {
    for (int i = 0; i < shellVars.count; i++) {
        printf("%s=%s\n", shellVars.keys[i], shellVars.values[i]);
    }
    return 1;
}

// history utility function + helpers 
void init_history() {
    history.recent_cmds = calloc(history_size_init, sizeof(char*));
    if (!history.recent_cmds) {
        printf("Error init history\n");
        exit(1);
    }
}

// update history everytime an input is provided
void update_history(char *command) {
    if (command == NULL || strcmp(command, "") == 0) {
        return;
    }

    // check for consequtive duplicate
    if (history.count > 0) {
        if (strcmp(history.recent_cmds[0], command) == 0) {
            return;
        }
    }

    // if full history, delete oldest command
    if (history.count == history.size) {
        free(history.recent_cmds[history.size - 1]);
        // shift all commands
        for (int i = history.size - 1; i > 0; --i) {
            history.recent_cmds[i] = history.recent_cmds[i - 1];
        }
    } else {
        // shift everything for new command
        for (int i = history.count; i > 0; --i) {
            history.recent_cmds[i] = history.recent_cmds[i - 1];
        }
        history.count++;
    }

    // add new command
    history.recent_cmds[0] = strdup(command);
}

// resize and reallocate history
void resize_history(int newSize) {
    if (newSize < 0) return; 
    char** newRecentCmds = calloc(newSize, sizeof(char*));
    if (!newRecentCmds) {
        printf("Error resizing history\n");
        return;
    }

    int currentCopySize = history.count < newSize ? history.count : newSize;
    for (int i = 0; i < currentCopySize; i++) {
        newRecentCmds[i] = strdup(history.recent_cmds[i]);
    }

    // free old history
    for (int i = 0; i < history.size; i++) {
        if (i < history.size - currentCopySize) {
            free(history.recent_cmds[i]);
        }
    }
    free(history.recent_cmds);

    history.recent_cmds = newRecentCmds;
    history.size = newSize;
    history.count = currentCopySize;
}

// parse input and determine correct history functionality
int rsh_history(char **args) {
    if (args[1]) {
        if (strcmp(args[1], "set") == 0 && args[2]) {
            int newSize = atoi(args[2]);
            if(strcmp(args[2], "0") == 0 || newSize != 0){
                resize_history(newSize);
                return 1;
            }
            else { printf("Numeric Only!\n"); return (-1); }
        }

        int index = atoi(args[1]);
        if (index > 0 && index <= history.count) {
            int pos = (history.startIdx + index - 1) % history.size;
            char* commandToExecute = strdup(history.recent_cmds[pos]);
            if (commandToExecute == NULL) {
                printf("Error allocating memory through <history n>\n");
                return 1; 
            }

            // Parse the command to execute
            char** argv = parse_input(commandToExecute);
            if (argv == NULL) {
                printf("Error parsing command for execution through <history n>\n");
                free(commandToExecute);
                return 1;
            }

            // execute command
            execute_input(argv); 
            
            free(commandToExecute);
            free(argv);
            return 1;
        } else { printf("Numeric Only!\n"); return (-1); }
    } else {
        // display history to stdout
        for (int i = 0; i < history.count; i++) {
            if (i == history.size) break; 
            int pos = (history.startIdx + i) % history.size; // circular indexing
            printf("%d) %s", i + 1, history.recent_cmds[pos]);
        }
    }
    return 1;
}

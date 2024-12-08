#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_CMD_LENGTH 1024
#define MAX_ARGS 100

typedef struct EnvVar {
    char *name;
    char *value;
    struct EnvVar *next;
} EnvVar;


EnvVar *envVars = NULL;


char* getEnvVar(const char *name) {
    EnvVar *current = envVars;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current->value;
        }
        current = current->next;
    }
    return NULL;
}


void setEnvVar(const char *name, const char *value) {
    EnvVar *current = envVars;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            free(current->value);
            current->value = strdup(value);
            return;
        }
        current = current->next;
    }
    
    EnvVar *newVar = (EnvVar *)malloc(sizeof(EnvVar));
    newVar->name = strdup(name);
    newVar->value = strdup(value);
    newVar->next = envVars;
    envVars = newVar;
}


void unsetEnvVar(const char *name) {
    EnvVar *current = envVars;
    EnvVar *previous = NULL;
    
    while (current) {
        if (strcmp(current->name, name) == 0) {
            if (previous) {
                previous->next = current->next;
            } else {
                envVars = current->next;
            }
            free(current->name);
            free(current->value);
            free(current);
            return;
        }
        previous = current;
        current = current->next;
    }
}


void replaceEnvVars(char *cmd) {
    char *start = cmd;
    while ((start = strchr(start, '$')) != NULL) {
        char *end = strchr(start + 1, ' ');
        if (!end) end = start + strlen(start);

        char varName[end - start - 1];
        strncpy(varName, start + 1, end - start - 1);
        varName[end - start - 1] = '\0';

        char *value = getEnvVar(varName);
        if (value) {
            char temp[MAX_CMD_LENGTH];
            strncpy(temp, cmd, start - cmd);
            temp[start - cmd] = '\0';
            strcat(temp, value);
            strcat(temp, end);
            strcpy(cmd, temp);
        }
        start = end;
    }
}


void executeCommand(char *cmd) {
    char *args[MAX_ARGS];
    int background = 0;
    int inputRedirect = 0;
    int outputRedirect = 0;
    char *inputFile = NULL;
    char *outputFile = NULL;

    char *token = strtok(cmd, " \t");
    int i = 0;
    
    while (token) {
        if (strcmp(token, "&") == 0) {
            background = 1;
        } else if (strcmp(token, "<") == 0) {
            inputRedirect = 1;
            inputFile = strtok(NULL, " \t");
        } else if (strcmp(token, ">") == 0) {
            outputRedirect = 1;
            outputFile = strtok(NULL, " \t");
        } else {
            args[i++] = token;
        }
        token = strtok(NULL, " \t");
    }
    args[i] = NULL;

    if (strcmp(args[0], "cd") == 0) {
        if (chdir(args[1]) != 0) {
            perror("cd failed");
        }
        return;
    } else if (strcmp(args[0], "pwd") == 0) {
        char cwd[MAX_CMD_LENGTH];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("pwd failed");
        }
        return;
    } else if (strcmp(args[0], "set") == 0) {
        setEnvVar(args[1], args[2]);
        return;
    } else if (strcmp(args[0], "unset") == 0) {
        unsetEnvVar(args[1]);
        return;
    } else if (strcmp(args[0], "echo") == 0) {
        for (int j = 1; args[j]; j++) {
            replaceEnvVars(args[j]);
            printf("%s ", args[j]);
        }
        printf("\n");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (inputRedirect) {
            int fdIn = open(inputFile, O_RDONLY);
            if (fdIn == -1) {
                perror("Input file open failed");
                exit(1);
            }
            dup2(fdIn, STDIN_FILENO);
            close(fdIn);
        }

        if (outputRedirect) {
            int fdOut = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fdOut == -1) {
                perror("Output file open failed");
                exit(1);
            }
            dup2(fdOut, STDOUT_FILENO);
            close(fdOut);
        }

        if (execvp(args[0], args) == -1) {
            perror("Command not found");
            exit(1);
        }
    } else {
        if (background) {
            printf("Process %d running in background\n", pid);
        } else {
            waitpid(pid, NULL, 0);
        }
    }
}


void executePipeline(char *cmd) {
    char *commands[MAX_ARGS];
    int numCommands = 0;

    char *token = strtok(cmd, "|");
    while (token) {
        commands[numCommands++] = token;
        token = strtok(NULL, "|");
    }

    int pipefd[2 * (numCommands - 1)];
    pid_t pid;

    for (int i = 0; i < numCommands - 1; i++) {
        if (pipe(pipefd + 2 * i) == -1) {
            perror("pipe failed");
            exit(1);
        }
    }

    for (int i = 0; i < numCommands; i++) {
        pid = fork();
        if (pid == 0) {
            if (i > 0) {
                dup2(pipefd[2 * (i - 1)], STDIN_FILENO);
            }
            if (i < numCommands - 1) {
                dup2(pipefd[2 * i + 1], STDOUT_FILENO);
            }

            for (int j = 0; j < 2 * (numCommands - 1); j++) {
                close(pipefd[j]);
            }

            char *args[MAX_ARGS];
            int j = 0;
            char *cmdArgs = commands[i];
            token = strtok(cmdArgs, " \t");
            while (token) {
                args[j++] = token;
                token = strtok(NULL, " \t");
            }
            args[j] = NULL;

            execvp(args[0], args);
            perror("execvp failed");
            exit(1);
        }
    }

    for (int i = 0; i < 2 * (numCommands - 1); i++) {
        close(pipefd[i]);
    }

    for (int i = 0; i < numCommands; i++) {
        wait(NULL);
    }
}


int main() {
    char cmd[MAX_CMD_LENGTH];
    
    while (1) {
        printf("xsh# ");
        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            break;
        }

        cmd[strcspn(cmd, "\n")] = '\0';

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        }

        if (strchr(cmd, '|')) {
            executePipeline(cmd);
        } else {
            executeCommand(cmd);
        }
    }

    EnvVar *current = envVars;
    while (current) {
        EnvVar *temp = current;
        current = current->next;
        free(temp->name);
        free(temp->value);
        free(temp);
    }

    return 0;
}

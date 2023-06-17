#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "stdio.h"
#include "errno.h"
#include "stdlib.h"
#include "unistd.h"
#include <string.h>
#include <signal.h>
#include "llist.h"

char *prompt;
int lastCommandStatus = -1;
char prevCommand[1024];
char tmpCommand[1024];
List commandsMemmory;
List variables;
int status = 0;
int runnung = 0;
int number_of_pipes = 0, PIPE_WRITER = 1, PIPE_READER = 0, counter = 0;
int fildes[2];
char *argv[10];

typedef struct Var
{
    char *key;
    char *value;
} Var;

char *searchVar(char *key)
{
    Node *h = variables.head;
    while (h)
    {
        if (!strcmp(((Var *)h->data)->key, key))
        {
            return ((Var *)h->data)->value;
        }
        h = h->next;
    }
    return NULL;
}

int numberOfCommands(char *str)
{
    if (!*str)
    {
        return 0;
    }

    int cnt = 0;
    while (*str)
    {
        if (*str == ' ')
        {
            cnt++;
        }
        str++;
    }
    return cnt + 1;
}

void changeCurrentDir(char *path)
{
    if (chdir(path) != 0)
    {
        printf("chdir() to %s failed\n", path);
        return;
    }
    printf("chdir() to %s\n", path);
}

void ctrlCHandler(int sig)
{
    printf("You typed Control-C!\n");
    if (runnung)
    { // default behavior
        signal(SIGINT, SIG_DFL);
        raise(SIGINT);
        signal(SIGINT, ctrlCHandler);
    }
}

int handleRedirection(char **argv, char **outfile, int size)
{
    if (size >= 2 && (!strcmp(argv[size - 2], ">") || !strcmp(argv[size - 2], ">>")))
    {
        *outfile = argv[size - 1];
        return STDOUT_FILENO;
    }
    else if (size >= 2 && !strcmp(argv[size - 2], "2>"))
    {
        *outfile = argv[size - 1];
        return STDERR_FILENO;
    }
    else if (size >= 2 && !strcmp(argv[size - 2], "<"))
    {
        *outfile = argv[size - 1];
        return STDIN_FILENO;
    }

    return -1;
}

void onInput(char *command)
{
    char *outfile;
    int i, fd, amper, redirect = -1, retid;
    char *token = strtok(command, " ");
    i = 0;

    while (token != NULL)
    {
        argv[i] = token;
        token = strtok(NULL, " ");
        i++;
    }
    argv[i] = NULL;

    /* Is command empty */
    if (argv[0] == NULL)
        return;

    if (strcmp(argv[0], "if") == 0)
    {
        system(command);
        return;
    }

    if (!strcmp(argv[0], "!!"))
    {
        strcpy(tmpCommand, prevCommand);
        onInput(tmpCommand);
        return;
    }

    if (argv[0][0] == '$' && i >= 3)
    {
        Var *var = (Var *)malloc(sizeof(Var));
        var->key = malloc((strlen(argv[0]) + 1));
        var->value = malloc((strlen(argv[2]) + 1));

        strcpy(var->key, argv[0]);
        strcpy(var->value, argv[2]);

        add(&variables, var);
        return;
    }

    if (!strcmp(argv[0], "read"))
    {
        Var *var = (Var *)malloc(sizeof(Var));
        var->key = malloc(sizeof(char) * (strlen(argv[1])));
        var->value = malloc(sizeof(char) * 1024);
        var->key[0] = '$';
        memset(var->value, 0, 1024);
        strcpy(var->key + 1, argv[1]);
        fgets(var->value, 1024, stdin);
        var->value[strlen(var->value) - 1] = '\0';
        add(&variables, var);
        return;
    }

    if (!strcmp(argv[0], "cd"))
    {
        changeCurrentDir(argv[1]);
        return;
    }

    if (!strcmp(argv[0], "prompt"))
    {
        free(prompt);
        prompt = malloc(strlen(argv[2]) + 1);
        strcpy(prompt, argv[2]);
        return;
    }

    if (!strcmp(argv[0], "echo"))
    {
        char **args = argv + 1;
        if (!strcmp(*args, "$?"))
        {
            printf("%d\n", status);
            return;
        }

        while (*args)
        {
            if (*args && *args[0] == '$')
            {
                char *v = searchVar(*args);
                if (v != NULL)
                    printf("%s ", v);
            }
            else
                printf("%s ", *args);
            args++;
        }
        printf("\n");
        return;
    }

    /* Does command line end with & */
    if (!strcmp(argv[i - 1], "&"))
    {
        amper = 1;
        argv[i - 1] = NULL;
    }
    else
        amper = 0;

    int redirectFd = handleRedirection(argv, &outfile, i);

    /* for commands not part of the shell command language */

    if (fork() == 0)
    {
        /* redirection of IO ? */
        if (redirectFd >= 0)
        {
            if (!strcmp(argv[i - 2], ">>"))
            {
                fd = open(outfile, O_WRONLY | O_CREAT);
                lseek(fd, 0, SEEK_END);
            }
            else if (!strcmp(argv[i - 2], ">") || !strcmp(argv[i - 2], "2>"))
            {
                fd = creat(outfile, 0660);
            }
            else
            {
                // stdin
                fd = open(outfile, O_RDONLY);
            }

            close(redirectFd);

            dup(fd);
            close(fd);
            /* stdout is now redirected */
            argv[i - 2] = NULL;
        }

        execvp(argv[0], argv);
    }
    /* parent continues here */
    if (amper == 0)
    {
        retid = wait(&status);
    }
}

/*
this function is for handling any pipes.
If the command contains a pipe, the function creates a pipe and forks the process to execute the command in the child process.
*/
int pipeController(int input_fd, int first_pipe, int last_pipe)
{

    if (argv[0] != NULL)
    {
        number_of_pipes += 1;
        int pipe_fd[2];

        /*
        create pipe() filling two fd to reading and writing.
        pipe_fd[0] --> for reading
        pipe_fd[1] --> for writing
        */
        pipe(pipe_fd); // creates a pair of file descriptors, one for reading from the pipe and one for writing to the pipe.

        if (fork() == 0)
        {

            // If it's the first pipe then it's a writer.
            if (input_fd == 0 && first_pipe == 1 && last_pipe == 0)
            {
                // dup2(fd1, fd2) fd2 point on what fd1 points.
                dup2(pipe_fd[PIPE_WRITER], STDOUT_FILENO);
            }
            // If it's an inner pipe command
            else if (input_fd != 0 && first_pipe == 0 && last_pipe == 0)
            {
                dup2(input_fd, STDIN_FILENO);
                dup2(pipe_fd[PIPE_WRITER], STDOUT_FILENO);
            }
            // If it's the last pipe
            else
            {
                dup2(input_fd, STDIN_FILENO);
            }

            execvp(argv[0], argv);
        }

        if (input_fd != 0)
        {
            close(input_fd);
        }

        // If there's no need to write
        close(pipe_fd[PIPE_WRITER]);

        // If there's no need to read
        if (last_pipe == 1)
        {
            close(pipe_fd[PIPE_READER]);
        }

        return pipe_fd[PIPE_READER];
    }

    return 0;
}

/*
takes a string containing a command as input and separates
it into individual words (tokens).
These tokens are stored in the argv array.
*/
int tokenParser(char *command)
{
    char *token;
    int i = 0;
    // get the first token (word)
    token = strtok(command, " ");

    // get all other words in the command line
    while (token != NULL)
    {
        argv[i] = token;

        // get the next word (token)
        token = strtok(NULL, " ");
        i++;
    }

    // mark the last token as NULL
    argv[i] = NULL;

    return i;
}

void waitingParentsPipes()
{
    // wait(NULL) will block parent process until any of its children has finished.
    for (int i = 0; i < number_of_pipes; ++i)
    {
        wait(NULL);
    }
}


// if date | grep Fri\n then\n echo "Shabat Shalom"\n else\n echo "Hard way to go" fi\n

  

  


int main()
{
    // signal(SIGINT, ctrlCHandler);
    char command[1024];
    prompt = malloc(7);
    strcpy(prompt, "hello:");
    int commandPosition = -1;
    char *b;
    int i;
    char ch;

    while (1)
    {
        printf("%s ", prompt);

        ch = getchar();
        if (ch == '\033')
        {
            printf("\033[1A"); // line up
            printf("\x1b[2K"); // delete line
            getchar();         // skip the [
            switch (getchar())
            {
            case 'A':
                // code for arrow up
                if (commandPosition > 0)
                {
                    commandPosition--;
                }
                printf("%s\n", (char *)get(&commandsMemmory, commandPosition));
                break;
            case 'B':
                // code for arrow down
                if (commandPosition < commandsMemmory.size - 1)
                {
                    commandPosition++;
                }
                printf("%s\n", (char *)get(&commandsMemmory, commandPosition));
                break;
            }
            getchar();
            continue;
        }

        command[0] = ch;
        fgets(command + 1, 1023, stdin);
        command[strlen(command) - 1] = '\0';

        // exit
        if (!strcmp(command, "quit"))
            break;

        // save last command
        if (strcmp(command, "!!"))
            strcpy(prevCommand, command);

        // add to commands memory list
        b = malloc(sizeof(char) * strlen(command));
        strcpy(b, command);
        add(&commandsMemmory, b);

        // update last command index
        commandPosition = commandsMemmory.size;

        if (strncmp(command, "if", 2) == 0)
        {
            system(command);
            continue;
        }

        i = 0;
        // find the first pipe sign.
        char *pipes = command;
        char *next_pipe = strchr(pipes, '|'); // returns pointer to the location of the character in the string,NULL otherwise.

        // if there's a pipe use in the command
        if (next_pipe != NULL)
        {

            int input_fd = 0, first_pipe = 1;

            while (next_pipe != NULL)
            {
                // replace the sign | with the sign \0 to find the next | if there is one
                *next_pipe = '\0';
                i = tokenParser(command);
                input_fd = pipeController(input_fd, first_pipe, 0); // returns pipe_fd[PIPE_READER] pointer for reading
                pipes = next_pipe + 1;                              // to the next pointer
                next_pipe = strchr(pipes, '|');
                first_pipe = 0;
            }

            // The last pipe
            i = tokenParser(pipes);
            input_fd = pipeController(input_fd, first_pipe, 1);
            waitingParentsPipes();
            number_of_pipes = 0; // init number of pipes in the system for next iteration.
            continue;
        }

        runnung = 0;
        // handle command
        onInput(command);
        runnung = 1;
    }
}

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
int number_of_pipes = 0, PIPE_WRITER = 1, PIPE_READER = 0, counter = 0;
int fildes[2];
char *argv[1024];
int stdoutfd;
pid_t runningProcces = -1;

enum states
{
    NEUTRAL,
    WANT_THEN,
    THEN_BLOCK,
    ELSE_BLOCK
};
enum results
{
    SUCCESS,
    FAIL
};

int if_state = NEUTRAL;
int if_result = SUCCESS;
int last_stat = 0;

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
    if (runningProcces != -1)
    { // default behavior
        kill(runningProcces, SIGKILL);
    }
    else
    {
        printf("You typed Control-C!\n");
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

void printArgs(char **args)
{
    char **p = args;
    while (*p != NULL)
    {
        fprintf(stderr, "%s ", *p);
        p++;
    }
    fprintf(stderr, "\n");
}

void splitCommand(char *command)
{
    char *token = strtok(command, " ");
    int i = 0;

    while (token != NULL)
    {
        argv[i] = token;
        token = strtok(NULL, " ");
        i++;
    }
    argv[i] = NULL;
}

char **findPipeCommand(char **args)
{
    char **p = args;
    while (*p != NULL)
    {
        if (strcmp(*p, "|") == 0)
        {
            return p;
        }

        p++;
    }

    return NULL;
}

int argsCount(char **args)
{
    char **p = args;
    int cnt = 0;
    while (*p != NULL)
    {
        p++;
        cnt++;
    }

    return cnt;
}

int execute(char **args)
{
    char *outfile;
    int i = argsCount(args), fd, amper, redirect = -1, rv = -1;
    pid_t pid;
    int hasPip = 0;
    // find the first pipe sign.
    char **pipPointer = findPipeCommand(args); // returns pointer to the location of the character in the string,NULL otherwise.
    int pipe_fd[2];

    // if there's a pipe use in the command
    if (pipPointer != NULL)
    {
        hasPip = 1;
        *pipPointer = NULL;
        i = argsCount(args);

        pipe(pipe_fd);

        if (fork() == 0)
        {
            close(pipe_fd[PIPE_WRITER]);
            close(STDIN_FILENO);
            dup(pipe_fd[PIPE_READER]);
            execute(pipPointer + 1);
            exit(0);
        }

        stdoutfd = dup(STDOUT_FILENO);
        dup2(pipe_fd[PIPE_WRITER], STDOUT_FILENO);
    }

    /* Is command empty */
    if (args[0] == NULL)
        return 0;

    if (!strcmp(args[0], "!!"))
    {
        strcpy(tmpCommand, prevCommand);
        splitCommand(tmpCommand);
        execute(argv);
        return 0;
    }

    if (args[0][0] == '$' && i >= 3)
    {
        Var *var = (Var *)malloc(sizeof(Var));
        var->key = malloc((strlen(args[0]) + 1));
        var->value = malloc((strlen(args[2]) + 1));

        strcpy(var->key, args[0]);
        strcpy(var->value, args[2]);

        add(&variables, var);
        return 0;
    }

    if (!strcmp(args[0], "read"))
    {
        Var *var = (Var *)malloc(sizeof(Var));
        var->key = malloc(sizeof(char) * (strlen(args[1])));
        var->value = malloc(sizeof(char) * 1024);
        var->key[0] = '$';
        memset(var->value, 0, 1024);
        strcpy(var->key + 1, args[1]);
        fgets(var->value, 1024, stdin);
        var->value[strlen(var->value) - 1] = '\0';
        add(&variables, var);
        return 0;
    }

    if (!strcmp(args[0], "cd"))
    {
        changeCurrentDir(args[1]);
        return 0;
    }

    if (!strcmp(args[0], "prompt"))
    {
        free(prompt);
        prompt = malloc(strlen(args[2]) + 1);
        strcpy(prompt, args[2]);
        return 0;
    }

    if (!strcmp(args[0], "echo"))
    {
        char **echo_args = args + 1;
        if (!strcmp(*echo_args, "$?"))
        {
            printf("%d\n", status);
            return 0;
        }

        while (*echo_args)
        {
            if (*echo_args && *echo_args[0] == '$')
            {
                char *v = searchVar(*echo_args);
                if (v != NULL)
                    printf("%s ", v);
            }
            else
                printf("%s ", *echo_args);
            echo_args++;
        }
        printf("\n");
        return 0;
    }

    /* Does command line end with & */
    if (!strcmp(args[i - 1], "&"))
    {
        amper = 1;
        args[i - 1] = NULL;
    }
    else
        amper = 0;

    int redirectFd = handleRedirection(args, &outfile, i);

    /* for commands not part of the shell command language */

    if ((runningProcces = fork()) == 0)
    {
        /* redirection of IO ? */
        if (redirectFd >= 0)
        {
            if (!strcmp(args[i - 2], ">>"))
            {
                fd = open(outfile, O_WRONLY | O_CREAT);
                lseek(fd, 0, SEEK_END);
            }
            else if (!strcmp(args[i - 2], ">") || !strcmp(args[i - 2], "2>"))
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
            args[i - 2] = NULL;
        }

        // fprintf(stderr,"%s--\n" , args[0]);
        // printArgs(args);
        execvp(args[0], args);
    }
    /* parent continues here */
    if (amper == 0)
    {
        wait(&status);
        rv = status;
        runningProcces = -1;
    }

    if (hasPip)
    {
        close(STDOUT_FILENO);
        close(pipe_fd[PIPE_WRITER]);
        dup(stdoutfd);
        wait(NULL);
    }

    return rv;
}

int process(char **args);
// if date | grep Fri\n then\n echo "Shabat Shalom"\n else\n echo "Hard way to go" fi\n

int do_contol_command(char **args)
{
    char *cmd = argv[0];
    int rv = -1;

    if (strcmp(cmd, "if") == 0)
    {
        if (if_state != NEUTRAL)
        {
            printf("if unexpected");
            rv = 1;
        }
        else
        {
            last_stat = process(args + 1);
            if_result = (last_stat == 0) ? SUCCESS : FAIL;
            if_state = WANT_THEN;
            rv = 0;
        }
    }
    else if (strcmp(cmd, "then") == 0)
    {
        if (if_state != WANT_THEN)
        {
            printf("then unexpected");
            rv = 1;
        }
        else
        {
            if_state = THEN_BLOCK;
            rv = 0;
        }
    }
    else if (strcmp(cmd, "else") == 0)
    {
        if (if_state != THEN_BLOCK)
        {
            printf("else unexpected");
            rv = 1;
        }
        else
        {
            if_state = ELSE_BLOCK;
            rv = 0;
        }
    }
    else if (strcmp(cmd, "fi") == 0)
    {
        if (if_state != THEN_BLOCK && if_state != ELSE_BLOCK)
        {
            printf("fi unexpected");
            rv = 1;
        }
        else
        {
            if_state = NEUTRAL;
            rv = 0;
        }
    }

    return rv;
}

int is_control_command(char *s)
{
    return (strcmp(s, "if") == 0 || strcmp(s, "then") == 0 || strcmp(s, "else") == 0 || strcmp(s, "fi") == 0);
}

int is_ok_execute()
{
    int rv = 1;
    if (if_state == WANT_THEN)
    {
        rv = 0;
    }
    else if (if_state == THEN_BLOCK && if_result == SUCCESS)
    {
        rv = 1;
    }
    else if (if_state == THEN_BLOCK && if_result == FAIL)
    {
        rv = 0;
    }
    else if (if_state == ELSE_BLOCK && if_result == FAIL)
    {
        rv = 1;
    }
    else if (if_state == ELSE_BLOCK && if_result == SUCCESS)
    {
        rv = 0;
    }
    // printf("execute? %d %d\n",rv , if_result);
    return rv;
}

int process(char **args)
{
    int rv = -1;
    // do control command
    if (args[0] == NULL)
    {
        rv = 0;
    }
    else if (is_control_command(args[0]))
    {
        rv = do_contol_command(args);
    }
    else if (is_ok_execute())
    {
        // 2- execute
        rv = execute(args);
    }

    return rv;
}

int main()
{
    signal(SIGINT, ctrlCHandler);
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

        splitCommand(command);

        // handle command
        status = process(argv);

        // printf("status: %d\n", status);
    }
}

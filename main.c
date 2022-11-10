#define _GNU_SOURCE
#define ARG_LIM 512
#define INPUT_LIM 2048

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

int childExitMethod = -5;
int SIGINT_flag = 0;
int SIGTSTP_flag = 0;
int backgroundAllowed = 1;                      // NOTE: 1 indicates processes are allowed to run in the background
pid_t incompleteBgProcesses[INPUT_LIM];
int incompleteBgProcessesNum;


/*
*   Redirects the input and/or output of a command that is about to be executed.
*   Modifies commandList if the input and/or output is changed such that the appropriate arguments are passed to the command
*/
void updateIO(char** commandList, int* commandNum, int backgroundFlag)
{
    int fileDescriptor;
    int redirectionFlag = 0;

    // Iterate through all commands in search of < and > operators
    for (int index = 1; index < *commandNum; index++)
    {
        // If the process should be run in the background and no file was specified, I/O becomes /dev/null
        if (backgroundFlag == 1)
        {
            fileDescriptor = open("/dev/null", O_RDONLY);
            int validate = dup2(fileDescriptor, STDIN_FILENO);

            // Print error upon unsuccessful redirection
            if (validate == -1)
            {
                perror("An error occurred while trying to redirect input");
                exit(1);
            }
        }

        // Check for input redirection
        if (strcmp(commandList[index], "<") == 0)
        {
            fileDescriptor = open(commandList[index + 1], O_RDONLY);
            int validate = dup2(fileDescriptor, STDIN_FILENO);

            // Print error upon unsuccessful redirection
            if (validate == -1)
            {
                printf("cannot open %s for input\n", commandList[index + 1]);
                fflush(stdout);
                exit(1);
            }

            redirectionFlag = 1;
        }

        // Check for output redirection
        if (strcmp(commandList[index], ">") == 0)
        {
            fileDescriptor = open(commandList[index + 1], O_CREAT | O_TRUNC | O_RDWR, 0744);
            int validate = dup2(fileDescriptor, STDOUT_FILENO);

            // Print error upon unsuccessful redirection
            if (validate == -1)
            {
                printf("cannot open %s for output\n", commandList[index + 1]);
                fflush(stdout);
                exit(1);
            }
            
            redirectionFlag = 1;
        }
    }

    // If redirection occurred, then commandList is modified so the appropriate commands are passed to the exec() call later
    if (redirectionFlag == 1)
    {
        for (int index = 1; index < *commandNum; index++)
        {
            commandList[index] = NULL;
        }

        close(fileDescriptor);
    }
}


/*
*   Checks if commands are meant to be run in the background (user included & at the end of their input).
*   Returns 1 if they should be run in the background, returns 0 if they should be run in the foreground.
*/
int checkBackground(char** commandList, int* commandNum)
{
    if (strcmp(commandList[*commandNum - 1], "&") == 0)
    {
        commandList[*commandNum - 1] = NULL;            // removes the "&" character from the array
        *commandNum -= 1;

        // If background processes are allowed, then the process will be set to run in the background
        if (backgroundAllowed != 0)
        {
            return 1;
        }
    }
    
    // If "&" was not found or background processes are not allowed, then the process will be set to run in the foreground
    return 0;
}


/*
*   Catches the SIGTSTP signal (when CTRL + Z pressed) and toggles foreground-only mode.
*/
void catchSIGTSTP(int signo)
{
    // Toggle flag
    backgroundAllowed = (backgroundAllowed == 0) ? 1: 0;

    // Print appropriate message
    if (backgroundAllowed == 0)
    {
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        SIGTSTP_flag = 1;
        write(STDOUT_FILENO, message, 50);
    }
    else
    {
        char* message = "Exiting foreground-only mode\n";
        SIGTSTP_flag = 0;
        write(STDOUT_FILENO, message, 30);
    }
    
    fflush(stdout);
}


/*
*   Handles commands that are not built into the shell (anything other than exit, cd, and status).
*   Forks the current process to handle bash commands and foreground/background process management.
*/
void executeOther(char** commandList, int* commandNum)
{
    int backgroundFlag;
    pid_t spawnpid = -5;
    childExitMethod = -5;

    // Special signal rules; ignore SIGINT and SIGTSTP
    struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};

    // Handle SIGTSTP signals based on catchSIGTSTP
    SIGTSTP_action.sa_handler = catchSIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = 0;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // Handle SIGINT signals by ignoring them
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Check if the process should run in the background (user entered &), set backgroundFlag accordingly
    backgroundFlag = checkBackground(commandList, commandNum);

    // Create child process
    spawnpid = fork();

    switch (spawnpid)
    {
        // If something went wrong, no child process was created and the system exits.
        case -1:
            perror("Fork failure!\n");
            exit(1); break;
        
        // Child process
        case 0:
            // Child processes running in foreground are terminated by SIGINT signals
            if (backgroundFlag == 0)
            {
                SIGINT_action.sa_handler = SIG_DFL;
                sigaction(SIGINT, &SIGINT_action, NULL);
            }

            // Redirect input/output accordingly and execute the command
            updateIO(commandList, commandNum, backgroundFlag);
            execvp(commandList[0], commandList);

            // Prints error message if there was an issue with the above execvp() call and exits
            fprintf(stderr, "'%s': no such file or directory\n", commandList[0]);
            exit(1);


        // Parent Process
        default:
        
            // If the child is running in the background:
            if (backgroundFlag == 1)
            {
                printf("background pid is %d\n", spawnpid);
                fflush(stdout);

                // Adds the background process' process ID to an integer array so it can be checked for and cleaned up later
                incompleteBgProcesses[incompleteBgProcessesNum] = spawnpid;
                incompleteBgProcessesNum++;
            }

            // If the child is running in the foreground:
            else
            {
                // Waits for child to terminate
                waitpid(spawnpid, &childExitMethod, 0);
                
                // Checks if there are potentially background processes running
                if (SIGTSTP_flag != 1)
                {
                    // Check if the child was prematurely terminated by a signal
                    if (WTERMSIG(childExitMethod) != 0 && WTERMSIG(childExitMethod) != 123 && WIFSIGNALED(childExitMethod) == 1)
                    {
                        printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
                        fflush(stdout);
                    }

                    // Checks for zombie (defunct) processes
                    if (waitpid(-1, &childExitMethod, WNOHANG) != 0)
                    {
                        // Checks each of incomplete background processes
                        for (int index = 0; index < incompleteBgProcessesNum; index++)
                        {
                            int currentPid = incompleteBgProcesses[index];
                            if (currentPid != -5)
                            {
                                // Requests status of the current child process without suspending execution
                                waitpid(currentPid, &childExitMethod, WNOHANG);

                                // Prints if the zombie child has exited
                                if (WIFEXITED(childExitMethod) != 0 && currentPid > 0)
                                {
                                    printf("background pid %d is done: exit value: %d\n", currentPid, WEXITSTATUS(childExitMethod));
                                    fflush(stdout);
                                    incompleteBgProcesses[index] = -5;
                                }

                                // Prints if the zombie child has been terminated by a signal
                                else if (WIFSIGNALED(childExitMethod) != 0 && currentPid > 0 && backgroundFlag == 0)
                                {
                                    printf("background pid %d is done: terminated by signal %d\n", currentPid, WTERMSIG(childExitMethod));
                                    fflush(stdout);
                                    incompleteBgProcesses[index] = -5;
                                }
                            }
                        }
                    }
                }
            }
    }
}


/*
*   Checks for built-in commands in the entered line.
*   Returns 1 if a built-in command was executed, otherwise returns 0
*/
int checkBuiltIn(char** commandList, int* commandNum)
{
    int commandExecFlag = 0;

    // Handling comments
    if (commandList[0][0] == '#')
    {
        commandExecFlag = 1;
    }

    // Checks for the exit command
    if (strcmp(commandList[0], "exit") == 0)
    {
        commandExecFlag = 1;
        fflush(stdout);
        while (1)
        {
            exit(0);
        }
    }

    // Checks for the cd command
    else if (strcmp(commandList[0], "cd") == 0)
    {
        commandExecFlag = 1;
        if (*commandNum == 1)
        {
            chdir(getenv("HOME"));
        }
        else
        {
            chdir(commandList[1]);
        }
    }

    // Checks for the status command
    else if (strcmp(commandList[0], "status") == 0)
    {
        commandExecFlag = 1;

        // Prints informative message if the last process was terminated by a signal
        if (WIFSIGNALED(childExitMethod))
        {
            printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
        }

        // Prints informative message if the last process was terminated by exiting
        else if (WIFEXITED(childExitMethod))
        {
            printf("exit value %d\n", WEXITSTATUS(childExitMethod));
        }
        fflush(stdout);
    }

    return commandExecFlag;
}


/*
*   Replaces "$$" with the shell's process ID
*   Returns the number of characters added to originalStr after the replacement has been made
*/
int replaceChars(char* originalStr, char* replacementStr, char* position)
{
    char startOfStr[INPUT_LIM] = "";
    char endOfStr[INPUT_LIM] = "";
    int originalLength = strlen(originalStr);

    // Copies the characters leading up to the $$
    strncpy(startOfStr, originalStr, (position - originalStr));
    startOfStr[position - originalStr] = '\0';

    // Copies the characters after $$
    memcpy(endOfStr, (position + 2), (strlen(originalStr) - strlen(startOfStr) - 2));
    sprintf(endOfStr, "%s\0", endOfStr);

    // Puts the whole string together
    sprintf(originalStr, "%s%s%s", startOfStr, replacementStr, endOfStr);
    return strlen(originalStr) - originalLength;
}


/*
*   Gathers the line of user input and formats it as an array of commands.
*   Modifies commandList to hold pointers to each of the command strings and modifies commandNum
*     to repesent the number of valid commands parsed.
*/
void getInput(char** commandList, int* commandNum)
{
    char userInput[INPUT_LIM];       // accepts max length of 2048 chars
    size_t bufferSize = INPUT_LIM;
    char* lineEntered = NULL;
    char* saveptr;
    pid_t smallshPid = getpid();

    printf(": ");
    fflush(stdout);

    // Gather user input from the line entered
    getline(&lineEntered, &bufferSize, stdin);              // user input command stored in lineEntered
    if (lineEntered == NULL)
    {
        strcpy(userInput, "");
    }
    else
    {
        strcpy(userInput, lineEntered);                     // copy input so it is not destroyed by string tokenizer
    }

    char* token = strtok_r(userInput, " \n", &saveptr);

    // Collects all commands with string tokenizer
    while (token != NULL)
    {
        char currentCommand[INPUT_LIM];
        strcpy(currentCommand, token);

        // Replaces $$ with the shell's process ID
        if (strstr(currentCommand, "$$") != NULL)
        {
            char* position = strstr(currentCommand, "$$");
            char smallshPidStr[6];                          // process IDs will not be larger than 99999, plus one character for null terminator
            sprintf(smallshPidStr, "%d", smallshPid);

            // Replaces all instances of $$ within the current command
            while (position != NULL)
            {
                replaceChars(currentCommand, smallshPidStr, position);
                position = strstr(currentCommand, "$$");
            }
        }

        // Creates and copies the string pointer to commandList
        commandList[*commandNum] = strdup(currentCommand);
        *commandNum += 1;

        token = strtok_r(NULL, " \n", &saveptr);
    }

    free(lineEntered);
    lineEntered = NULL;
}


/*
*   Runs the main program execution.
*   Continually prompts the user until they enter the "exit" command.
*/
int main()
{
    char* commandList[ARG_LIM];
    int commandNum;

    // Repeatedly asks for user input
    while (1)
    {
        fflush(stdin);
        fflush(stdout);
        commandNum = 0;

        // Gathers user's input
        getInput(commandList, &commandNum);

        // Continues to the next iteration if the user did not enter any commands
        if (commandNum == 0)
        {
            continue;
        }

        // Spawns child processes if no built-in commands were entered
        if (checkBuiltIn(commandList, &commandNum) == 0)
        {
            executeOther(commandList, &commandNum);
        }

        // Removes all commands from commandList for the next iteration
        for (int index = 0; index < commandNum; index++)
        {
            commandList[index] = NULL;
        }
    }
}

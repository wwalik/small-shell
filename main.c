#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define ARG_LIM 512
#define INPUT_LIM 2048

int childExitMethod = -5;
int SIGINT_flag = 0;
int SIGTSTP_flag = 0;
int backgroundAllowed = 1;         // 1 indicates background is allowed


void updateIO(char** commandList, int* commandNum, int backgroundFlag)
{
    int redirectionFlag = 0;
    int fileDescriptor;

    // Iterate through all commands in search of < and > operators
    for (int index = 1; index < *commandNum; index++)
    {
        // If the process should be run in the background and no file was specified, I/O should be /dev/null
        if (backgroundFlag == 1)
        {
            fileDescriptor = open("/dev/null", O_RDONLY);
            int validate = dup2(fileDescriptor, STDIN_FILENO);
            if (validate != -1)
            {
                redirectionFlag = 1;
            }
            else
            {
                perror("An error occurred while trying to redirect input/output.");
                exit(1);
            }
        }

        // Update input
        if (strcmp(commandList[index], "<") == 0)
        {
            fileDescriptor = open(commandList[index + 1], O_RDONLY);
            int validate = dup2(fileDescriptor, STDIN_FILENO);
            if (validate != -1)
            {
                // printf("Input updated to %s\n", commandList[index + 1]);
                // fflush(stdout);
                redirectionFlag = 1;
            }
            else
            {
                perror("An error occurred while trying to redirect input.");
                exit(1);
            }
        }

        // Update output
        if (strcmp(commandList[index], ">") == 0)
        {
            fileDescriptor = open(commandList[index + 1], O_CREAT | O_TRUNC | O_RDWR, 0744);
            int validate = dup2(fileDescriptor, STDOUT_FILENO);
            if (validate != -1)
            {
                // printf("Output updated to %s\n", commandList[index + 1]);
                // fflush(stdout);
                redirectionFlag = 1;
            }
            else
            {
                perror("An error occurred while trying to redirect output.");
                exit(1);
            }
        }
    }

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
*   Checks if commands are meant to be run in the background (user included & at the end of the command list).
*   Returns 1 if they should be run in the background, returns 0 if they should be run in the foreground.
*/
int checkBackground(char** commandList, int* commandNum)
{
    if (strcmp(commandList[*commandNum - 1], "&") == 0)
    {
        *commandNum -= 1;
        commandList[*commandNum - 1] = NULL;            // removes the character from the array
        return 1;
    }
    
    // "&" was not found, run in foreground
    return 0;
}


/*
*   Catches the SIGTSTP signal (when CTRL + Z pressed), toggles foreground-only mode.
*/
void catchSIGTSTP(int signo)
{
    // Toggle flag
    backgroundAllowed = (backgroundAllowed == 0) ? 1: 0;

    // Print proper message
    if (backgroundAllowed == 0)
    {
        char* message = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50);
    }
    else
    {
        char* message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30);
    }
    SIGTSTP_flag = 1;
    fflush(stdout);
}

void executeOther(char** commandList, int* commandNum)
{
    int backgroundFlag;
    pid_t spawnpid = -5;
    childExitMethod = -5;
    // Special signal rules; ignore SIGINT and SIGSTP
    struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};

    // Handle SIGTSTP signals based on custom function defined above
    SIGTSTP_action.sa_handler = catchSIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = 0;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // Handle SIGINT signals by ignoring them
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Create child process
    spawnpid = fork();
    // printf("Created process with id %d\n", spawnpid);
    // fflush(stdout);

    // Check if the process should run in the background (user entered &), set backgroundFlag
    if (backgroundAllowed == 1)
    {
        backgroundFlag = checkBackground(commandList, commandNum);
    }
    else
    {
        backgroundFlag = 0;         // if background processes are not allowed, then the background flag is set to 0 (process will run in foreground)
    }
    // printf("Current value of backgroundFlag: %d\n", backgroundFlag);
    // fflush(stdout);

    switch (spawnpid)
    {
        // If something went wrong, no child process created and parent process gets return value of -1
        case -1:
            perror("Fork failure!\n");
            exit(1); break;
        
        // Child process
        case 0:
            // printf("Child process is running.\n");
            // fflush(stdout);
            if (backgroundFlag == 0)
            {
                SIGINT_action.sa_handler = SIG_DFL;
                sigaction(SIGINT, &SIGINT_action, NULL);
            }
            // Update input/output and execute the command
            updateIO(commandList, commandNum, backgroundFlag);
            if (SIGINT_flag != 1 && SIGTSTP_flag != 1 && execvp(commandList[0], commandList))
            {
                fprintf(stderr, "The '%s' command could not be found.\n", commandList[0]);
                exit(1);
            }

            if (backgroundFlag == 1)
            {
                // Continues as long as waitpid() does not return due to the delivery of a signal to the calling process
                while (spawnpid != -1)
                {
                    // Requests status of any child process without suspending execution
                    spawnpid = waitpid(-1, &childExitMethod, WNOHANG);

                    // Prints once the child has exited
                    if (WIFEXITED(childExitMethod) && spawnpid > 0)
                    {
                        int exitStatus = WEXITSTATUS(childExitMethod);
                        printf("background pid %d is done: exit value: %d\n", spawnpid, exitStatus);
                        fflush(stdout);
                    }

                    // Prints once the child has been terminated by a signal
                    else if (WIFSIGNALED(childExitMethod) && spawnpid > 0 && backgroundAllowed == 0)
                    {
                        int termSignal = WTERMSIG(childExitMethod);
                        printf("background pid %d is done: terminated by signal %d\n", spawnpid, termSignal);
                        fflush(stdout);
                    }
                }
            }

            exit(0); break;

        // Parent Process
        default:
            // printf("Parent process is running. Value of backgroundFlag: %d\n", backgroundFlag);
            // fflush(stdout);
            // Checks if the child is running in the background
            if (backgroundFlag == 1)
            {
                printf("background pid is %d\n", spawnpid);
                fflush(stdout);
            }
            else
            {
                // Waits for child to terminate
                waitpid(spawnpid, &childExitMethod, 0);
                
                // Checks if there could potentially be background processes running
                if (SIGTSTP_flag != 1) 
                {
                    // Check if the child was terminated by a signal
                    if (WTERMSIG(childExitMethod) != 0 && WIFSIGNALED(childExitMethod) == 1)
                    {
                        int termSignal = WTERMSIG(childExitMethod);
                        printf("terminated by signal %d\n", termSignal);
                        fflush(stdout);
                    }

                    // Continues as long as waitpid() does not return due to the delivery of a signal to the calling process
                    while (spawnpid != -1)
                    {
                        // Requests status of any child process without suspending execution
                        spawnpid = waitpid(-1, &childExitMethod, WNOHANG);

                        // Prints once the child has exited
                        if (WIFEXITED(childExitMethod) && spawnpid > 0)
                        {
                            int exitStatus = WEXITSTATUS(childExitMethod);
                            printf("background pid %d is done: exit value: %d\n", spawnpid, exitStatus);
                            fflush(stdout);
                        }

                        // Prints once the child has been terminated by a signal
                        else if (WIFSIGNALED(childExitMethod) && spawnpid > 0 && backgroundAllowed == 0)
                        {
                            int termSignal = WTERMSIG(childExitMethod);
                            printf("background pid %d is done: terminated by signal %d\n", spawnpid, termSignal);
                            fflush(stdout);
                        }
                    }
                }
            }
        break;
    }
    // Executed by both parent and child
}


/*
*   Checks for built-in commands in the entered line.
*   Returns 0 if a built-in command was executed, otherwise returns 1
*/
int checkBuiltIn(char** commandList, int* commandNum)
{
    int commandExecFlag = 1;

    // Handling Comments
    if (commandList[0][0] == '#')
    {
        commandExecFlag = 0;
        // printf("Comment detected. Continuing to the next iteration!\n");
        // fflush(stdout);
    }

    // Checks for the exit command
    if (strcmp(commandList[0], "exit") == 0)
    {
        commandExecFlag = 0;
        fflush(stdout);
        while (1)
        {
            exit(0);
        }
    }

    // Checks for cd command
    else if (strcmp(commandList[0], "cd") == 0)
    {
        commandExecFlag = 0;
        if (*commandNum == 1)
        {
            chdir(getenv("HOME"));
        }
        else
        {
            chdir(commandList[1]);
        }
    }

    // Checks for status command
    else if (strcmp(commandList[0], "status") == 0)
    {
        commandExecFlag = 0;
        if (WIFEXITED(childExitMethod))
        {
            printf("exit value %d\n", WEXITSTATUS(childExitMethod));
        }
        else if (WIFSIGNALED(childExitMethod))
        {
            printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
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
*   Gathers the line of user input and arrages it as an array of commands.
*/
void getInput(char** commandList, int* commandNum)
{
    char userInput[INPUT_LIM];       // accepts max length of 2048 chars
    size_t bufferSize = INPUT_LIM;
    char* lineEntered = NULL;
    char* userInputPtr = NULL;
    char* saveptr;
    pid_t smallshPid = getpid();

    printf(": ");
    fflush(stdout);

    getline(&lineEntered, &bufferSize, stdin);      // user input command stored in lineEntered
    strcpy(userInput, lineEntered);                     // copy input so it is not destroyed by string tokenizer
    char* token = strtok_r(userInput, " \n", &saveptr);

    if (token == NULL)
    {
        // printf("Blank line detected. Continuing to the next iteration!\n");
        // fflush(stdout);
    }

    // Collect the tokens
    while (token != NULL)
    {
        char currentCommand[INPUT_LIM];
        strcpy(currentCommand, token);
        //printf("Current command: '%s'\n", currentCommand);
        //fflush(stdout);

        // Replacing $$ with process ID
        if (strstr(currentCommand, "$$") != NULL)
        {
            char* position = strstr(currentCommand, "$$");
            char smallshPidStr[6];                          // process IDs will not be larger than 99999, plus one character for null terminator
            sprintf(smallshPidStr, "%d", smallshPid);

            while (position != NULL)
            {
                replaceChars(currentCommand, smallshPidStr, position);
                position = strstr(currentCommand, "$$");
            }
        }

        commandList[*commandNum] = strdup(currentCommand);
        *commandNum += 1;

        token = strtok_r(NULL, " \n", &saveptr);
    }

    free(lineEntered);
    lineEntered = NULL;
}

/*
*   Runs the main program execution.
*   Continually prompts the user until they decide they are finished processing files.
*/
int main()
{
    char* commandList[ARG_LIM];
    int commandNum;

    // Repeatedly asks for user input
    while (1)
    {
        commandNum = 0;
        fflush(stdout);

        getInput(commandList, &commandNum);

        // Spawns child processes if no built-in commands were entered
        if (checkBuiltIn(commandList, &commandNum) == 1)
        {
            executeOther(commandList, &commandNum);
        }

        // Reset commandList and commandNum for the next iteration

        for (int index = 0; index < commandNum; index++)
        {
            commandList[index] = NULL;
        }
        int commandNum;
    }
}
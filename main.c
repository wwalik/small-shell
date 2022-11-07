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
                perror("An error occurred while trying to open /dev/null.");
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
                redirectionFlag = 1;
            }
            else
            {
                perror("An error occurred while trying to open the file.");
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
                redirectionFlag = 1;
            }
            else
            {
                perror("An error occurred while trying to open the file.");
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
        commandList[*commandNum] = NULL;            // removes the character from the array
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
    int childExitStatus = -5;
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

    // Check if the process should run in the background (user entered &), set backgroundFlag
    if (backgroundAllowed == 1)
    {
        int backgroundFlag = checkBackground(commandList, commandNum);
    }
    else
    {
        int backgroundFlag = 0;         // if background processes are not allowed, then the background flag is set to 0 (process will run in foreground)
    }

    // Create child process
    spawnpid = fork();
    switch (spawnpid)
    {
        // If something went wrong, no child process created and parent process gets return value of -1
        case -1:
            perror("Fork failure!\n");
            exit(1); break;
        
        // Child process
        case 0:
            printf("Child process is running.\n");
            if (backgroundFlag == 0)
            {
                SIGINT_action.sa_handler = SIG_DFL;
                sigaction(SIGINT, &SIGINT_action, NULL);
            }
            // Update input/output and execute the command
            updateIO(commandList, commandNum, backgroundFlag);
            if (SIGINT_flag != 1 && SIGTSTP_flag != 1 && execvp(commandList[0], commandList))
            {
                fprintf(stderr, "The '%s' command could not be executed because it is unknown.", commandList[0]);
                exit(1);
            }
            break;

        // TODO: IMPLEMENT PARENT PROCESS
        // Parent Process
        default:
            printf("Parent process is running.\n");


            exit(0); break;
    }
    // Executed by both parent and child
}


/*
*   Checks for built-in commands in the entered line.
*   Returns 0 if a built-in command was executed, otherwise returns 1
*/
int checkBuiltIn(char** commandList, int* commandNum, int status)
{
    int commandExecFlag = 1;
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
        if (WIFEXITED(status))
        {
            printf("Exit status: %d\n", WEXITSTATUS(status));
        }
        else if (WIFSIGNALED(status))
        {
            printf("Terminating signal: %d\n", WTERMSIG(status));
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
        printf("Blank line detected. Continuing to the next iteration!\n");
        fflush(stdout);
    }

    // Collect the tokens
    while (token != NULL)
    {
        char currentCommand[INPUT_LIM];
        strcpy(currentCommand, token);
        printf("Current command: '%s'\n", currentCommand);
        fflush(stdout);

        // Handling Comments
        if (currentCommand[0] == '#')
        {
            printf("Comment detected. Continuing to the next iteration!\n");
            fflush(stdout);
        }
        // Replacing $$ with process ID
        else if (strstr(currentCommand, "$$") != NULL)
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
    int status;

    // Repeatedly asks for user input
    while (1)
    {
        commandNum = 0;
        fflush(stdout);

        getInput(commandList, &commandNum);

        // printf("Command list: ");
        // for (int index = 0; index < commandNum; index++)
        // {
        //     if (commandList[index] != NULL)
        //     {
        //         printf("%s, ", commandList[index]);
        //     }
        // }
        // printf("\n");

        // Spawns child processes if no built-in commands were entered
        if (checkBuiltIn(commandList, &commandNum, status) == 1)
        {
            executeOther(commandList, &commandNum);
        }
    }
}
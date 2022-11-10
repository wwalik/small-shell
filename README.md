# Small Shell 
*Author: Julia Melchert | Date Last Updated: 11/7/2022*

This program creates a shell that accounts for the following:
* Provide a prompt for running commands
* Handle blank lines and comments, which are lines beginning with the # character
* Provide expansion for the variable $$
* Execute 3 commands exit, cd, and status via code built into the shell
* Execute other commands by creating new processes using a function from the exec family of functions
* Support input and output redirection
* Support running commands in foreground and background processes
* Implement custom handlers for 2 signals, SIGINT and SIGTSTP

It utilizes the **C** programming language.

You can compile and run this program with the following commands:
```
gcc --std=gnu99 -o smallsh main.c
./smallsh
```

You can run the test script by placing it in the same directory as the compiled shell, calling chmod (*chmod +x ./p3testscript*) upon first use, and then typing the following command:
```
./p3testscript 2>&1
```
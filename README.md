# rsh -- A simple UNIX Shell

---

![image](https://github.com/RamiE965/rsh/assets/60044975/a6fb8561-6c5b-4fe3-b06f-ac4dcd3274f2)

## Description
RSH (Rami Shell) is a simple UNIX shell that supports basic command-line functionalities including executing commands with arguments, handling environment and shell variables, pipes, simple history management, and a set of built-in commands.

### Features
- Piping
- Enivornment & Shell Variables
- Built-in Commands: exit, cd, export, local, vars, & history
- History Management
- Command Execution
- Interactive & Batch Modes

## Usage 
#### Interactive Mode
Simply run `./rsh` after compiling the c file. Then start typing your commands!

#### Batch Mode
Run `./rsh script.rsh` where the `script.rsh` is a file that contains a list of commands that you intend to run in batch mode. 

#### Shell Variables
- For setting local variables `local MYSHELLVARNAME=somevalue`
- For setting enviornment variables `export MYENVVARNAME=somevalue`

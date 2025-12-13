# Shell

A simple UNIX-style shell implemented in C.

---

## Feature
- Keeps a few list of **built-commands** to run such as `exit`, `?`, `pwd` and `cd`
- Executes **external programs** by searching for executables  `PATH` environment variable 
- Supports **input and output redirection** for external commands (e.g `wc < textfile > outputfile`)
- Supports **pipelined commands** (e.g `wc shell.c shell.c shell.c | grep total`)

---

## Build Instructions
```bash
make
```
Running `make` produces the `shell` executable in the current directory

---

## Usage

Start the shell:
```bash
./shell
```

Example interactive session: 
```python repl
0: ?  
? - show this help menu
exit - exit the command shell
pwd - prints the current working directory
cd - changes the current working directory
1: pwd
/home/mubaaroq/Desktop/systems/hw-shell
2: wc shell.c
 295 1055 7825 shell.c
3: wc shell.c shell.c shell.c | grep total
  885  3165 23475 total
4: wc < shell.c > test_out
5: cat test_out
 295 1055 7825
7: cat shell.c shell.c shell.c | wc | grep 885
    885    3165   23475
8: exit
```

## Credits/Acknowledgements
- Based on skeleton code from **UC Berkeley CS162** coursework

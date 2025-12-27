# Shell

A simple UNIX-style shell implemented in C.

---

## Features
- Keeps a few list of **built-commands** to run such as `exit`, `?`, `pwd` and `cd`
- Executes **external programs** by searching for executables  `PATH` environment variable 
- Supports **input and output redirection** for external commands (e.g `wc < textfile > outputfile`)
- Supports **pipelined commands** (e.g `wc shell.c shell.c shell.c | grep total`)
- Supports running processes in the **background**

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

### Example interactive session: 

Built-in commands: 
```python repl
0: ?  
? - show this help menu
exit - exit the command shell
pwd - prints the current working directory
cd - changes the current working directory
wait - block until all background jobs have terminated
1: pwd
/home/mubaaroq/Desktop/systems/hw-shell
2: cd ..
3: pwd
/home/mubaaroq/Desktop/systems/
```

Executing external programs:
```python repl
0: wc shell.c
 295 1055 7825 shell.c
1: ls /home/mubaaroq
 Desktop  Documents  Downloads  Music  Pictures	Public	snap  Templates  Videos
```

Pipeline commands and input/output redirection:
```python repl
0: wc shell.c shell.c shell.c | cat | grep total
 885  3165 23475 total
1: wc < shell.c > test_out
2: cat test_out
 295 1055 7825
```

Executing commands in foreground or background:
```python repl
0: sleep 20 | sleep 30
^C1: 
2: sleep 20 | sleep 30 &  
3: ps -o pid,pgid,wchan,cmd
    PID    PGID WCHAN  CMD
   5392    5392 do_wai bash
   6029    6029 do_wai ./shell
   6079    6079 hrtime sleep 20
   6080    6079 hrtime sleep 30
   6081    6081 -      ps -o pid,pgid,wchan,cmd
4: wait
5: ps -o pid,pgid,wchan,cmd
    PID    PGID WCHAN  CMD
   5392    5392 do_wai bash
   6029    6029 do_wai ./shell
   6095    6095 -      ps -o pid,pgid,wchan,cmd

```

## Credits / Acknowledgements
- Based on skeleton code from **UC Berkeley CS162** coursework

# Operating_Systems
S16OS is a bare bones Operating System simulation that is created in C.
See how I used this to understand and create my own programming language: [Potato Programming language](https://github.com/Auroxsus/Potato_Programming_Language).

> "Although each machine has a different assembly language, the assembly process is sufficiently similar on different machines that it
is possible to describe it in general.[^1].

[^1]: _Structured Computer Organization (6th edition) ©2013 by Andrew Tanenbaum and Todd Austin, pages 529-530_

Here is the foundation of our OS:
* `S16.c` : Operating System Simulation
* `S16Assembler.c` : Assembly language assembler
* `S16ComputerSystems.doc` : Documents the S16 hardware
* `S16Assembler.doc` : Documents the S16 assembly language

Sample Programs are kindly provided for reference on future development.
It's encouraged to use Dev-C++ as there is no guarentee that Visual Studio and other IDE's are able to run without errors. Other IED's will want you to find the String configuration to something more secure.

We are going to practice Operating System concepts with the following:
* `S16.exe` : simulates the S16 OS by building `S16.c`
* `S16Assembler.exe` : translates the source programs into object programs by building `S16Assembler.c`
* `[filename].s16` : assembly language source program
* `[filename].object` : object programs are loaded into S16 memory and executed
* `[filename].job` : script that instructs the system on how to run a batch job
* `[filename].listing` : identifies source line(s) containing syntax errors found in `[filenmae].s16`

How to Use:
1. Download all files {`S16.c`, `S16Assebler.c`, `Computer.h`, `Computer.c`, `LabelTable.h`, `LabelTable.c`,  `Random.h`, `Random.c`, `S16.config`, `SVCDefinitions.txt`.} into a single folder
2. Build the Modules for `S16Assembler.c` and for `S16.c` to create `S16Assembler.exe` and `S16.exe`
> [!TIP]
> You must build `S16Assembler.exe` and `S16.exe` using a _C programming language_ project, not a _C++_ project!
> * For S16Assembler Project, it __must__ contain the following source files: `S16Assebler.c`, `Computer.h`, `LabelTable.h`, `LabelTable.c`
> * For S16 Project, it __must__ contain the following source files: `S16.c`, `Computer.h`, `Computer.c`, `LabelTable.h`, `LabelTable.c`,  `Random.h`, `Random.c`
3. Download `Sample1.job` and the `Sample.s16` and place them in your folder.
> [!IMPORTANT]
> Ensure that your S16 content folder has a copy of the configuartion file `S16.config`

> [!NOTE]
> Assuming that the S16 assembly language source file `Sample1.s16` is stored in the same folder as the S16Assembler load module
`S16Assembler.exe`, S16Assembler can be run without a command line argument (which causes S16Assembler to prompt for the source file
name)
> ```
> C:Operating_Systems\S16Simulation\Code\S16>S16Assembler
> S16 source filename [.s16]? Sample1
> ```
> or with the source file name provided as the only command line argument
>
>```
>C:C:Operating_Systems\S16Simulation\Code\S16>S16Assembler Sample1
>```
4. Run `S16Assembler.exe` to assemble `Sample1.s16`. This creates a `Sample1.object` and `Sample1.listing`
> [!IMPORTANT]
> When executed to translate an open-able `.s16` source file, the S16 assembler always produces a `.listing` file, but will produce an `.object` file *only* when the assembly process ends without finding syntax
5. Run `S16.exe` to run the jobstream `Sample1.job`. This creates a `Sample1.trace`

> [!CAUTION]
> When the input file is stored in a folder different from the folder that contains the _S16_ load module, the input file path must also be specified. For example,
> ```
> ".\JobStreams\Sample1"
> ```
> The path `".\JobStreams\"` becomes the `WORKINGDIRECTORY` which is then automatically prefixed to the input file name `.config` file passed to the`CREATE_CHILD_PROCESS` system service request.

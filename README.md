# Simulating GPUs in CADSS: Divergence Simulator

Authors: _Theo Kroening_, _Ethan Lu_

This directory is a standalone CADSS project for simulating thread divergence
and different techniques for mitigating thread divergence. Because we are only interested in studying divergence on one thread, this simulator does not integrate multiple SMs or warps. For that, see the _Scaling Simulator_.

## Building the Simulator

This is a CMake project. To build, just run:
```
cmake .
make
```

From there, you should be able to run different simulations.

## Using the Transpiler

This project provides a very basic transpiler from PTX into a restricted subset of PTX we call "PTX-Minus" (inspired by the "PTX-Plus" of the GPGPUSim project). The transpiler is located in the `transpiler` directory. Example usage:
```
cd transpiler
python3 transpiler.py ptx_saxpy.txt saxpy.txt
```

Note that, due to time constraints, the transpiler does not implement support for the entirety of the PTX ISA, just the subset needed to run experiments on the bundled example programs.

## Example Programs

We bundle the small set of example programs used in the report to demonstrate the capabilities of our simulator. All traces can be found in the `traces` directory.

### Binary Search

The `trace_binsearch.txt` trace runs the parallel binary search kernel from Written Assignment 2. There is also "hard" variant (`trace_binsearch_hard.txt`) which searches through more elements.

### Collatz

The `collatz.txt` program computes the number of iterations to converge under the Collatz conjecture for each of the input elements in parallel. The computational intensity of each task increases as we increase the thread ID.


## Running Experiments

The general format of a CADSS invocation looks like:
```
./cadss-engine -p gpu -s config/reconvergence.config -t traces/
collatz.txt
```

The _config_ files (the "s") flag can be used to toggle between the two schedulers investigated in the report:
* `config/reconvergence.config` -- The Reconverge Stack implementation.
* `config/naive.config` -- A naive implementation that does not attempt to reconverge threads.

By default, the above command does not output any information besides the total number of ticks needed to execute the trace. Adding the verbose flag (`-v`) shows what's happening under the hood, for example active lane masks at each instruction, how the CFG is built, as well as post-dominators and immediate post-dominators.

The thread activity charts in the report were collected simply by leaving the verbose flag off and adding a few print statements.

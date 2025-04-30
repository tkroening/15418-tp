Computer Architecture Design Simulator for Students

This repository contains a multi-component integrated simulator to support
computer architecture students writing simulation models of different
computer architecture components, such as caches, branch predictors, or
coherence protocols.  The provided code handles trace reading and
initialization of simple components.

Reference implentations are available upon request.

Other support code and files can be found at https://github.com/bprail/cadss_support.

# README for Scaling simulator
This is the final submission of the Scaling simulator. This simulator is not able to do computation on the trace, but is instead used to model how factors such as number of threads, the parallelism capaiblity of a SM (warp slots), barriers, and number of SM cores impact computation time in ticks. 

## build process
1. Add clone the repo
2. Run cmake .
3. run make
4. run the command gdb --args ./cadss-engine -n 1 -c<cache_or_refCache> -o refCoherence -p gpu -t traces/<your_trace.trace>  -s ex_proc.config

    
## how to configure
1. Thread Blocks: change the BLOCKS define in gpu/gpu.cpp
2. threads in a block: change the THREADSPERBLOCK define in gpu/gpu.cpp
3. number of SM cores: change the NUMSM define in gpu/gpu.cpp
4. number of threads in a SM: change the THREADSPERWARP define in gpu/gpu.cpp
5. number of warp slots on a SM: change the MAXWARPS define in gpu/sm.h
6. number of registers a warp has: change the REGISTER_COUNT of gpu/sm.h

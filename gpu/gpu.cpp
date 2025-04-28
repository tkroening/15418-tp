#include <getopt.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern "C" {
#include "branch.h"
#include "cache.h"
#include "processor.h"
#include "trace.h"
}

#include "sm.h"
#define TOTALTHREADS 32 * 1;
#define THREADSPERBLOCK 32 * 1;
#define BLOCKS 1;
#define THREADSPERWARP 32;
#define NUMSM 1;

trace_reader *tr = NULL;
cache *cs = NULL;
branch *bs = NULL;

// processor* self = NULL;
std::vector<SM *> streaming_multiprocessors;

int processorCount = 1;
int CADSS_VERBOSE = 0;

int *pendingMem = NULL;
int *pendingBranch = NULL;
int64_t *memOpTag = NULL;

// Need this prototype so the reference is defined in `init`:
void memOpCallback(int, int64_t);

void memOpCallback(int sm_id, int64_t tag) {
  auto sm = streaming_multiprocessors[0];

  // Notify the processor
  printf("got data from warp %d\n", sm_id);
  bool processorHadPendingRequest = sm->handleMemOpCallback(tag);
  assert(processorHadPendingRequest);
}

std::deque<std::pair<trace_op *, uint64_t>> dqueueTop;
void parseInstructions() {
  trace_op *op;

  while (true) {
    // TODO: Hardcoded PID 0 because all warps will be getting same instructions
    // anyway (?)
    op = tr->getNextOp(0);
    // if we reach end of trace file we break out of loop
    if (op == NULL) {
      std::cout << "Finished reading tracefile (hit NULL). Read " << std::endl;
      break;
    } else {
      // adds the op onto the instruction queue of all initialized threads

      // if (currWarp->warpState != UNINITIALIZED)
      (dqueueTop).push_back({op, -1}); // NOTE: -1 as we don't know which
                                       // slot they belong in
    }
  }
}

//
// init
//
//   Parse arguments and initialize the processor simulator components
//
extern "C" processor *init(processor_sim_args *psa) {
  int op;
  int totalThreads = TOTALTHREADS;
  int totalWarps = totalThreads / THREADSPERWARP;
  tr = psa->tr;
  cs = psa->cache_sim;
  bs = psa->branch_sim;

  // TODO: Replace with something relevant to SMs. For now, this is a dummy
  ProcessorArgs processor_args;

  // TODO - get argument list from assignment
  while ((op = getopt(psa->arg_count, psa->arg_list, "f:d:m:j:k:c:")) != -1) {
    switch (op) {
    // fetch rate
    case 'f':
      break;

    // dispatch queue multiplier
    case 'd':
      break;

    // Schedule queue multiplier
    case 'm':
      break;

    // Number of fast ALUs
    case 'j':
      break;

    // Number of long ALUs
    case 'k':
      break;

    // Number of CDBs
    case 'c':
      break;
    }
  }

  pendingBranch = (int *)calloc(processorCount, sizeof(int));
  pendingMem = (int *)calloc(processorCount, sizeof(int));
  memOpTag = (int64_t *)calloc(processorCount, sizeof(int64_t));
  // parseInstructions();

  processor *self = new processor;
  self->si.tick = tick;
  self->si.finish = finish;
  self->si.destroy = destroy;

  // Initialize all streaming multiprocessors -- just one for now
  uint num_SMs = 1;
  for (int SMID = 0; SMID < num_SMs; SMID++) {
    // SM(void (*memOpCallback)(int, int64_t), ProcessorArgs args, processor
    // *self, trace_reader *tr, cache *cs, branch *bs, int activeWarps, int
    // smid);

    SM *new_sm = new SM(memOpCallback, processor_args, self, tr, cs, bs,
                        totalWarps, // TODO: Why is activeWarps an int? Why is
                                    // it passed in the constructor
                        SMID, dqueueTop);

    streaming_multiprocessors.push_back(new_sm);
  }

  return self;
}

const int64_t STALL_TIME = 100000;
int64_t tickCount = 0;
int64_t stallCount = -1;

extern "C" int tick(void) {
  // if room in pipeline, request op from trace
  //   for the sample processor, it requests an op
  //   each tick until it reaches a branch or memory op
  //   then it blocks on that op

  trace_op *nextOp = NULL;

  // Pass along to the branch predictor and cache simulator that time ticked
  bs->si.tick();
  cs->si.tick();
  tickCount++;

  std::cout << std::endl;
  std::cout << "Tick: " << tickCount << std::endl;

  if (tickCount == stallCount) {
    printf("Processor may be stalled.  Now at tick - %ld, last op at %ld\n",
           tickCount, tickCount - STALL_TIME);
    for (int i = 0; i < processorCount; i++) {
      if (pendingMem[i] == 1) {
        printf("Processor %d is waiting on memory\n", i);
      }
    }
  }

  int progress = 0;
  for (auto &sm : streaming_multiprocessors) {
    /*
        Process pipeline phases backwards
        - Fetch
        - Decode
        - Execute
        - Mem
        - WB
    */

    progress |= sm->WriteBack();
    progress |= sm->Mem_falling();
    progress |= sm->Mem();
    progress |= sm->Execute();
    progress |= sm->Decode();
    progress |= sm->Fetch();
  }

  return progress;
}

extern "C" int finish(int outFd) {
  /*
      TODO: This needs to be updated for streaming multiprocessors
  */
  int c = cs->si.finish(outFd);
  int b = bs->si.finish(outFd);

  char buf[32];
  size_t charCount = snprintf(buf, 32, "Ticks - %ld\n", tickCount);

  (void)!write(outFd, buf, charCount + 1);

  if (b || c)
    return 1;
  return 0;
}

extern "C" int destroy(void) {
  /*
      TODO: This needs to be updated for streaming multiprocessors
  */
  int c = cs->si.destroy();
  int b = bs->si.destroy();

  if (b || c)
    return 1;
  return 0;
}

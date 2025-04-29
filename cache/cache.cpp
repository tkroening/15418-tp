extern "C" {
#include <cache.h>
#include <trace.h>
}

#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <vector>

typedef struct _pendingRequest {
  int64_t tag;
  int8_t procNum;
  void (*memCallback)(int, int64_t);
  int64_t count;
} pendingRequest;

cache *self = NULL;
coher *coherComp = NULL;

int processorCount = 1;
int CADSS_VERBOSE = 0;
std::vector<pendingRequest> pending;
int countDown = 0;

void memoryRequest(trace_op *op, int processorNum, int64_t tag,
                   void (*callback)(int, int64_t));
void coherCallback(int type, int procNum, int64_t addr);

extern "C" cache *init(cache_sim_args *csa) {
  int op;

  // TODO - get argument list from assignment
  while ((op = getopt(csa->arg_count, csa->arg_list, "E:s:b:i:R:")) != -1) {
    switch (op) {
    // Lines per set
    case 'E':
      break;

    // Sets per cache
    case 's':
      break;

    // block size in bits
    case 'b':
      break;

    // entries in victim cache
    case 'i':
      break;

    // bits in a RRIP-based replacement policy
    case 'R':

      break;
    }
  }

  self = (cache *)malloc(sizeof(cache));
  self->memoryRequest = memoryRequest;
  self->si.tick = tick;
  self->si.finish = finish;
  self->si.destroy = destroy;

  coherComp = csa->coherComp;
  coherComp->registerCacheInterface(coherCallback);

  return self;
}

// This routine is a linkage to the rest of the memory hierarchy
void coherCallback(int type, int procNum, int64_t addr) {
  switch (type) {
  case NO_ACTION:
  case DATA_RECV:
    // TODO: check that the addr is the pending access
    //  This indicates that the cache has received data from memory
    countDown = 1;
    break;

  case INVALIDATE:
    // This is taught later in the semester.
    break;

  default:
    break;
  }
}

void memoryRequest(trace_op *op, int processorNum, int64_t tag,
                   void (*callback)(int, int64_t)) {
  assert(op != NULL);
  assert(callback != NULL);

  // Simple model to only have one outstanding memory operation
  //   if (countDown != 0) {
  //     assert(pending.memCallback != NULL);
  //     pending.memCallback(pending.procNum, pending.tag);
  //   }

  pendingRequest pendingElement = (pendingRequest){.tag = tag,
                                                   .procNum = processorNum,
                                                   .memCallback = callback,
                                                   .count = 100};

  pending.push_back(pendingElement);
}

extern "C" int tick() {
  // Advance ticks in the coherence component.
  for (int i = 0; i < pending.size(); i++) {
    pendingRequest &currPending = pending[i];
    currPending.count--; // countdown
    if (currPending.count == 0) {
      assert(currPending.memCallback != NULL);
      currPending.memCallback(currPending.procNum, currPending.tag);
    }
  }
  return 1;
}

extern "C" int finish(int outFd) { return 0; }

extern "C" int destroy(void) {
  // free any internally allocated memory here
  free(self);
  return 0;
}
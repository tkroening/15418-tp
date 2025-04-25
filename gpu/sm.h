#include <array>
#include <cstdint>
#include <optional>
#include <queue>
#include <unordered_map>

extern "C" {
#include "branch.h"
#include "cache.h"
#include "processor.h"
#include "trace.h"
}

// TODO: This is a placeholder value for REGISTER_COUNT. Replace this!
#define REGISTER_COUNT 18

// TODO: This is a placeholder value for MAXWARPS. Replace this!
#define MAXWARPS 64

typedef struct {
  int regNum; /** @brief The register's architectural number. */
  bool ready; /** @brief Whether the register's value is ready or being
                 calculated. */
} Register_t;

typedef struct {
  trace_op *instr;
  uint64_t warpID;
  int64_t tag;
} mem_waiting_t; // an entry of data waiting for memory

/** @brief The arguments that need to be given to the Processor. */
struct ProcessorArgs {
  int d; /** Dispatch queue multiplier */
  int f; /** Fetch rate (instructions per cycle) */
  int m; /** Schedule queue multiplier */
  int j; /** Number of "fast" ALUs */
  int k; /** Number of "long" ALUs */
  int c; /** Number of CDBs */
};

/** @brief A warp can either be running, runnable, stalled, or not initialized .
 */
typedef enum state {
  RUNNING, // multiple warps can be running at same time due to piplelined arch
  RUNNABLE,
  STALLED, // when memory stalls
  UNINITIALIZED,
  FINISHED
} state_t;

typedef struct warp {
  state_t warpState;
  /** @brief The register file, need 1 for each warp */
  std::array<Register_t, REGISTER_COUNT> rf_;

  /** @brief The instruction queue, storing trace ops and their ids.
      need 1 for each warp*/
  std::deque<std::pair<trace_op *, uint64_t>>
      dq_; // need double queue as you need to peak at instructions in the
           // front

  /** @brief Sorted queue of finished instructions. */

  // TODO: Replaced vector type - idk what was going here before
  std::vector<int> finished_instructions_;
} warp_t;

class SM {
public:
  // Constructor
  SM(void (*memOpCallback)(int, int64_t), ProcessorArgs args, processor *self,
     trace_reader *tr, cache *cs, branch *bs, int activeWarps, int smid);

  ProcessorArgs args_;

  // to keep track of how many warps seen the barrier;
  int stallCount_;

  /** @brief The processor simulator pointer. */
  processor *ps_;

  /** @brief The trace reader pointer. */
  trace_reader *tr_;

  /** @brief The cache simulator pointer. */
  cache *cs_;

  /** @brief The branch predictor simulator pointer, not current focus*/
  branch *bs_;

  /**the queue of active warps waiting for a slot */
  std::queue<warp_t *> waitingWarps;

  std::vector<warp_t> allWarps; // need vector because size is variable

  int instructionCount_;

  bool Fetch();
  bool Decode();
  bool Execute();
  bool Mem();
  bool Mem_falling();
  bool WriteBack();
  bool handleMemOpCallback(int64_t tag);
  std::pair<trace_op *, uint64_t> scheduler();

private:
  /** @brief The sm number, currently just doing 1 */
  int smid_;

  /** @brief the number of warps in use */
  int activeWarps_;

  /** @brief structure that stores all info about warps, similar to thread
   * control block*/
  std::array<warp_t *, MAXWARPS> warps_;

  /** @brief Whether  a pending branch request. */
  std::optional<uint64_t> pending_branch_;

  /** @brief Function pointer for memOpCallback */
  void (*memOpCallback_)(int, int64_t);

  /*
      Various queues. Recall the classic five-stage pipeline:
      Fetch -> Decode -> Execute -> Mem -> Write Back
  */

  /** @brief The queue of instructions going into decode stage
      produced by fetch and consumed by decode*/
  std::queue<std::pair<trace_op *, uint64_t>>
      fetch_decode_queue_; // should always have length 0 or 1;

  /** @brief The queue of instructions going into execute stage
       produced by deocde and consumed by execute*/
  std::queue<std::pair<trace_op *, uint64_t>>
      decode_execute_queue_; // should always have length 0 or 1;

  /** @brief The queue of instructions going into memory stage
      produced by execute and consumed by memory*/
  std::queue<std::pair<trace_op *, uint64_t>>
      execute_mem_queue_; // should always have length 0 or 1;

  /** @brief The queue of instructions going into memory_falling stage
    produced by memory_rising and consumed by memory_falling*/
  std::queue<std::pair<trace_op *, uint64_t>>
      mem1_mem2_queue_; // should always have length 0 or 1;

  /** @brief The queue of instructions going into write back stage
     produced by memory_falling and consumed by wb */
  std::queue<std::pair<trace_op *, uint64_t>>
      mem2_wb_queue_; // should always have length 0 or 1;

  /** @brief the queue of instructions that has gotten their data from memory*/
  std::queue<std::pair<trace_op *, uint64_t>>
      mem_ready_queue_; // should always have length 0 or 1;

  /** @brief a vector of instructions currently waiting for data from memory*/
  std::vector<mem_waiting_t> mem_waiting_vector;

  /** Memory ticks stalling*/
  int memTickDelayCounter;

  /** @brief Memory delay Buffer */
  std::queue<std::pair<trace_op *, uint64_t>> memDelayBuffers;
};
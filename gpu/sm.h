#include <array>
#include <cstdint>
#include <queue>
#include <unordered_map>

extern "C" {
#include "branch.h"
#include "cache.h"
#include "trace.h"
}

typedef struct {
  int regNum; /** @brief The register's architectural number. */
  bool ready; /** @brief Whether the register's value is ready or being
                 calculated. */
} Register_t;

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
  std::vector<std::pair<proc_sq_slot_t, int>> finished_instructions_;
} warp_t;

class SM {
public:
  // Constructor
  SM(void (*memOpCallback)(int, int64_t), ProcessorArgs args, processor *self,
     trace_reader *tr, cache *cs, branch *bs, int activeWarps, int smid);

  /** @brief The processor simulator pointer. */
  processor *ps_;

  /** @brief The trace reader pointer. */
  trace_reader *tr_;

  /** @brief The cache simulator pointer. */
  cache *cs_;

  /** @brief The branch predictor simulator pointer, not current focus*/
  branch *bs_;

  int instructionCount_;

  bool Fetch();
  bool Decode();
  bool Execute();
  bool Mem();
  bool WriteBack();

private:
  /** @brief The sm number, currently just doing 1 */
  int smid_;

  /** @brief the number of warps in use */
  int activeWarps_;

  /** @brief structure that stores all info about warps, similar to thread
   * control block*/
  std::array<warp_t, MAXWARPS> warps;

  /** @brief Whether  a pending branch request. */
  std::optional<uint64_t> pending_branch_;

  /** @brief Function pointer for memOpCallback */
  void (*memOpCallback_)(int, int64_t);

  /** @brief The queue of instructions going into decode stage
      produced by fetch and consumed by decode*/
  std::queue<std::pair<trace_op *, uint64_t>>
      deq_; // should always have length 0 or 1;

  /** @brief The queue of instructions going into decode stage
       produced by deocde and consumed by execute*/
  std::queue<std::pair<trace_op *, uint64_t>>
      deq_; // should always have length 0 or 1;

  /** @brief The queue of instructions going into memory stage
      produced by execute and consumed by memory*/
  std::queue<std::pair<trace_op *, uint64_t>>
      memq_; // should always have length 0 or 1;

  /** @brief The queue of instructions going into write back stage
     produced by memory and consumed by wb */
  std::queue<std::pair<trace_op *, uint64_t>>
      wbq_; // should always have length 0 or 1;
};
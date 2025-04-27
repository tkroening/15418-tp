#ifndef SM_H
#define SM_H

#include <array>
#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <variant>

extern "C" {
  #include "processor.h"
  #include "trace.h"
}

#include "cfg.h"
#include "scheduler.h"

// Forward Declaration
class Scheduler;

// TODO: This is a placeholder value for REGISTER_COUNT. Replace this!
#define REGISTER_COUNT 18

// Debug Helper
void dbg_printf(const char *fmt, ...);

/*
    TODO: Hardcoded to 1 to implement control flow. Replace this!
*/
#define MAXWARPS 1

// TODO: Could perhaps be made into a configurable value in the future
#define THREADSPERWARP 32

using Value = std::variant<int32_t, int64_t, uint32_t, uint64_t, float, double>;

class Register {
  public:
    std::string register_name_; /** @brief Name as it appears in the trace file */
    bool ready_; /** @brief Whether the register's value is ready or being 
                    calculated. */

    /*
        All registers are considered to be "wide" - they hold one value for
        every thread in a warp.
    */
    std::array<Value, THREADSPERWARP> register_values_;
};

/** @brief The arguments that need to be given to the Processor. */
struct ProcessorArgs {
  int d; /** Dispatch queue multiplier */
  int f; /** Fetch rate (instructions per cycle) */
  int m; /** Schedule queue multiplier */
  int j; /** Number of "fast" ALUs */
  int k; /** Number of "long" ALUs */
  int c; /** Number of CDBs */
};

typedef struct _sm_instruction_t {
  int instruction_idx; /* Index in the original trace file */
  int warp_id; /* Which warp does this instruction belong to? */
  trace_op *t_op; /* The actual instruction */
  std::vector<bool> active_mask;
} sm_instruction_t;

/** @brief A warp can either be running, runnable, stalled, or not initialized .
 */
typedef enum state {
  RUNNING, // multiple warps can be running at same time due to piplelined arch
  RUNNABLE,
  STALLED_MEMORY, // when memory stalls
  UNINITIALIZED,
  FINISHED
} state_t;

typedef struct warp {
  state_t warpState;
  
  // Map register names to register information
  std::unordered_map<std::string, Register> rf_;

  // Current lane mask
  // std::array<bool, THREADSPERWARP> active_mask;

  std::array<bool, THREADSPERWARP> finished_mask;

  // Is there a control hazard to stall for?
  bool has_active_control_hazard;
} warp_t;

class SM {
public:
  // Constructor
  SM(void (*memOpCallback)(int, int64_t), ProcessorArgs args, processor *self,
     trace_reader *tr, int activeWarps, int smid);

  ProcessorArgs args_;

  /** @brief The processor simulator pointer. */
  processor *ps_;

  /** @brief The trace reader pointer. */
  trace_reader *tr_;

  int instructionCount_;

  bool Fetch();
  bool Decode();
  bool Execute();
  bool Mem();
  bool WriteBack();

  /* Return (mutable) pointer to information about the warp */
  warp_t *GetWarpPointer(int warp_id);
  CFG *GetCFG() { return cfg_; };

  std::vector<trace_op *> GetInstructions();

  // For debugging in gdb
  Value GetRegisterValue(int warp_id, const char *reg_name, int id);

  private:
    /** @brief The sm number, currently just doing 1 */
    int smid_;

    /** @brief the number of warps in use */
    int activeWarps_;

    /** @brief structure that stores all info about warps, similar to thread
     * control block*/
    std::array<warp_t, MAXWARPS> warps_;

    /** @brief Whether  a pending branch request. */
    std::optional<uint64_t> pending_branch_;

    /** @brief Function pointer for memOpCallback */
    void (*memOpCallback_)(int, int64_t);

    /*
        A copy of the instructions that make up the input program
    */
    std::vector<trace_op *> instructions_;

    Scheduler *scheduler_{};
    CFG *cfg_{};

    /*
        Various queues. Recall the classic five-stage pipeline:
        Fetch -> Decode -> Execute -> Mem -> Write Back
    */

    /** @brief The queue of instructions going into decode stage
        produced by fetch and consumed by decode*/
    std::queue<sm_instruction_t *>
        fetch_decode_queue_; // should always have length 0 or 1;

    /** @brief The queue of instructions going into execute stage
         produced by deocde and consumed by execute*/
    std::queue<sm_instruction_t *>
        decode_execute_queue_; // should always have length 0 or 1;

    /** @brief The queue of instructions going into memory stage
        produced by execute and consumed by memory*/
    std::queue<sm_instruction_t *>
        execute_mem_queue_; // should always have length 0 or 1;

    /** @brief The queue of instructions going into write back stage
       produced by memory and consumed by wb */
    std::queue<sm_instruction_t *>
        mem_wb_queue_; // should always have length 0 or 1;

    /*
        "Computation" Methods
    */

    // "Wide" - Returns value for every thread in the warp
    std::vector<Value> GetValueFromSource(operand_t * src, uint64_t warp_id);

    void DoComputation(sm_instruction_t * sm_instr);
  };

#endif
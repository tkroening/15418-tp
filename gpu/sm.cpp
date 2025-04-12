#include "sm.h"

#include <iostream>
#include <stdio.h>

// Constructor
SM::SM(void (*memOpCallback)(int, int64_t), ProcessorArgs args, processor *self,
       trace_reader *tr, cache *cs, branch *bs, int activeWarps, int smid)
    : memOpCallback_(memOpCallback), args_(args), ps_(self), tr_(tr), cs_(cs),
      bs_(bs), smid_(smid) {

  instructionCount_ = 0; // question: do I need self????
  // initialize registers and state of each warp
  for (int i = 0; i < MAXWARPS; i++) {
    warp_t *currWarp = &(warps[i]); // question: do I need self.warps[i]????

    // only intialize active warps
    if (i < activeWarps) {
      // all warps are initially runnable
      currWarp->warpState = RUNNABLE;

      // Initialize the register file to have all registers be ready.
      for (int reg = 0; reg < REGISTER_COUNT; reg++) {
        currWarp->rf_[reg] = {.register_num = reg, .tag = 0, .ready = true};
      }
    } else {
      currWarp->warpState = UNINITIALIZED;
    }
  }

  /** @brief moves all ops onto instruction queue of warps */
  trace_op *op;
  while (true) {
    op = tr_->getNextOp(pid_);
    // if we reach end of trace file we break out of loop
    if (op == NULL) {
      prinf("we got an null op!!\n");
      break;
    } else {
      // adds the op onto the instruction queue of all initialized threads
      for (int i = 0; i < MAXWARPS; i++) {
        warp_t *currWarp = &(warps[i]);
        if (currWarp->warpState != UNINITIALIZED)
          (currWarp->dq_).push_back({op, i});
      }
      instructionCount_++;
      assert((warps[0].dq_).size() == instructionCount_);
    }
  }
  assert((warps[0].dq_).size() == instructionCount_);

  // QUESTION: when we read all ops do we

  // initialize insturction queue of each warp

  std::cout << "SM constructor" << std::endl;
}

/******************************************************************************
 Fetch
 ******************************************************************************/

/**
 * @brief Performs the instruction fetch stage of the pipeline.
 *
 * Schedules the warp to execute the next struction
 * @return True if any instruction can be executed next
 */

bool Processor::Fetch() {
  bool progress{false};

  std::pair<trace_op *, uint64_t> instrPair = scheduler();

  trace_op *currentInstruction = instrPair.first;
  int warpNumber = instrPair.second;

  warp_t *scheduledWarp = &(warps[warpNumber]);

  // update register files
  for (auto src : I->src_reg) {
    // If the register is -1, it means no register is required.
    if (src == -1) {
      continue;
    }
    scheduledWarp->rf_[src].ready = false;
  }

  deq_.push(instrPair);
  return progress;
}

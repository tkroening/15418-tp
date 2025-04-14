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
 scheduler
 ******************************************************************************/

/**
 * @brief Schedules the next warp to be executed
 *
 * Looks for warps that are
 * 1. intitialized and not finished
 * 2. not stalled from memory
 * 3. does not have any hazards
 * @return True if any instructions were successfully fetched.
 */
std::pair<trace_op *, uint64_t> Processor::scheduler() {
  for (int i = 0; i < MAXWARPS; i++) {
    if (warps[i].warpState == FINISHED || warps[i].warpState == UNITIALIZED)
      continue;
    else if (warps[i].warpState == STALLED)
      continue;
    else if (warps[i].warpState == RUNNABLE) {

      // checks that there is no hazard
      // ASSUMES that rs1 and rs2 can not be 0
      trace_op *warpNextInstr = warps[i].dq_.front();
      int rs1 = trace_op->src_reg[0];
      int rs2 = trace_op->src_reg[1];
      if (rs1 != 0 && warps[i].rf_[rs1].ready == false)
        continue;
      else if (rs2 != 0 && warps[i].rf_[rs2].ready == false)
        continue;
      else {
        // no register conflicts, can return
        int selectedWarp = i;
        trace_op *warpInstr = warps[i].dq_.pop();
        std::pair<trace_op *, uint64_t> returnPair;
        returnPair.first = warpInstr;
        returnPair.second = selectedWarp;
        return returnPair;
      }
    }
  }
  // all warps are stalled
  int selectedWarp = -1;
  trace_op *warpInstr = NULL;
  std::pair<trace_op *, uint64_t> returnPair;
  returnPair.first = warpInstr;
  returnPair.second = selectedWarp;
  return returnPair;
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

  // do not make progress if all warps are stalled
  if (currentInstruction == NULL && warpNumber == -1)
    return false;

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

/******************************************************************************
 Decode
 ******************************************************************************/

/**
 * @brief Literally just stall 1 cycle
 *
 * @return True if any instruction can be executed next
 */

bool Processor::decode() {}

/******************************************************************************
 Execute
 ******************************************************************************/

/**
 * @brief Literally just stall 1 cycle
 *
 * @return True if any instruction can be executed next
 */

bool Processor::execute() {}

/******************************************************************************
 Memory
 ******************************************************************************/

/**
 * @brief Case on trace op and delay accordingly
 *
 * @return unsure what the return types are
 */

bool Processor::Memory() {}

/******************************************************************************
 Write back
 ******************************************************************************/

/**
 * @brief Write back stalls 1 cycle and updates the register file
 *
 * @return unsure what the return types are
 */

bool Processor::Memory() {}

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
    warp_t *currWarp = &(warps_[i]); // question: do I need self.warps[i]????

    // only intialize active warps
    if (i < activeWarps) {
      // all warps are initially runnable
      currWarp->warpState = RUNNABLE;

      // Initialize the register file to have all registers be ready.
      for (int reg = 0; reg < REGISTER_COUNT; reg++) {
        currWarp->rf_[reg] = {.regNum = reg, .ready = true};
      }
    } else {
      currWarp->warpState = UNINITIALIZED;
    }
  }

  /** @brief moves all ops onto instruction queue of warps */
  trace_op *op;
  while (true) {
    // TODO: Hardcoded PID 0 because all warps will be getting same instructions anyway (?)
    op = tr_->getNextOp(0);
    // if we reach end of trace file we break out of loop
    if (op == NULL) {
      std::cout << "We got a NULL op!!" << std::endl;
      break;
    } else {
      // adds the op onto the instruction queue of all initialized threads
      for (int i = 0; i < MAXWARPS; i++) {
        warp_t *currWarp = &(warps_[i]);
        if (currWarp->warpState != UNINITIALIZED)
          (currWarp->dq_).push_back({op, i});
      }
      instructionCount_++;
      assert((warps_[0].dq_).size() == instructionCount_);
    }
  }
  assert((warps_[0].dq_).size() == instructionCount_);

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

// TODO: I think this was meant to belogn to the SM class? This needs to be double-checked.
std::pair<trace_op *, uint64_t> SM::scheduler() {
  for (int i = 0; i < MAXWARPS; i++) {
    if (warps_[i].warpState == FINISHED || warps_[i].warpState == UNINITIALIZED)
      continue;
    else if (warps_[i].warpState == STALLED)
      continue;
    else if (warps_[i].warpState == RUNNABLE) {

      // checks that there is no hazard
      // ASSUMES that rs1 and rs2 can not be 0
      auto [warp_next_instr, warp_id] = warps_[i].dq_.front();
      
      // Idk 
      assert(warp_id == i);

      // trace_op *warpNextInstr = warps_[i].dq_.front();

      int rs1 = warp_next_instr->src_reg[0];
      int rs2 = warp_next_instr->src_reg[1];

      if (rs1 != -1 && warps_[i].rf_[rs1].ready == false)
        continue;
      else if (rs2 != -1 && warps_[i].rf_[rs2].ready == false)
        continue;
      else {
        // no register conflicts, can return
        int selectedWarp = i;

        // trace_op *warpInstr = warps_[i].dq_.pop();
        warps_[i].dq_.pop_front(); // TODO: Check if this is right
        
        std::pair<trace_op *, uint64_t> returnPair;
        
        // TODO: Was this the intended instruction
        returnPair.first = warp_next_instr;
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

bool SM::Fetch() {
  bool progress{false};

  std::pair<trace_op *, uint64_t> instrPair = scheduler();

  trace_op *currentInstruction = instrPair.first;
  int warpNumber = instrPair.second;

  // do not make progress if all warps are stalled
  if (currentInstruction == NULL && warpNumber == -1)
    return false;

  warp_t *scheduledWarp = &(warps_[warpNumber]);

  // update register files
  
  // TODO: What was "I" meant to be?
  for (auto src : currentInstruction->src_reg) {
    // If the register is -1, it means no register is required.
    if (src == -1) {
      continue;
    }
    scheduledWarp->rf_[src].ready = false;
  }

  fetch_decode_queue_.push(instrPair);
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

bool SM::Decode() {
  return false;
}

/******************************************************************************
 Execute
 ******************************************************************************/

/**
 * @brief Literally just stall 1 cycle
 *
 * @return True if any instruction can be executed next
 */

bool SM::Execute() {
  return false;
}

/******************************************************************************
 Memory
 ******************************************************************************/

/**
 * @brief Case on trace op and delay accordingly
 *
 * @return unsure what the return types are
 */

bool SM::Mem() {
  return false;
}

/******************************************************************************
 Write back
 ******************************************************************************/

/**
 * @brief Write back stalls 1 cycle and updates the register file
 *
 * @return unsure what the return types are
 */

bool SM::WriteBack() {
  return false;
}

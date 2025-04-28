#include "sm.h"

#include <iostream>
#include <stdio.h>
#include <sys/types.h>

int64_t makeTag(int procNum, int64_t baseTag) {
  return ((int64_t)procNum) | (baseTag << 8);
}

// Constructor
SM::SM(void (*memOpCallback)(int, int64_t), ProcessorArgs args, processor *self,
       trace_reader *tr, cache *cs, branch *bs, int activeWarps, int smid,
       std::deque<std::pair<trace_op *, uint64_t>> dqueueTop)
    : memOpCallback_(memOpCallback), args_(args), ps_(self), tr_(tr), cs_(cs),
      bs_(bs), smid_(smid), dqueue_(dqueueTop) {
  instructionCount_ = 0; // question: do I need self????
  // initialize registers and state of each warp

  activeWarps_ = activeWarps;
  stallCount_ = 0;
  allWarps.resize(activeWarps);

  // make all warps uninitialized
  for (int i = 0; i < activeWarps; i++) {
    warp_t *currWarp = &(allWarps[i]);
    currWarp->warpState = UNINITIALIZED;
  }

  // Might not need this for loop due to previous for loop
  for (int i = 0; i < MAXWARPS; i++) {
    warp_t *currWarp = &(allWarps[i]); // question: do I need self.warps[i]????
    currWarp->warpState = UNINITIALIZED;
  }

  /** @brief moves all ops onto instruction queue of warps */
  trace_op *op;

  while (true) {
    // TODO: Hardcoded PID 0 because all warps will be getting same instructions
    // anyway (?)
    op = tr_->getNextOp(0);
    // if we reach end of trace file we break out of loop
    if (op == NULL) {
      std::cout << "Finished reading tracefile (hit NULL). Read "
                << instructionCount_ << " instructions." << std::endl;
      break;
    } else {
      // adds the op onto the instruction queue of all initialized threads
      for (int i = 0; i < activeWarps; i++) {
        warp_t *currWarp = &(allWarps[i]);
        // if (currWarp->warpState != UNINITIALIZED)
        (currWarp->dq_).push_back({op, -1}); // NOTE: -1 as we don't know which
                                             // slot they belong in
      }
      instructionCount_++;
      assert((allWarps[0].dq_).size() == instructionCount_);
    }
  }

  // move data from allWarps to waitingWarps;
  for (int i = 0; i < activeWarps; i++) {
    warp_t *currWarp = &(allWarps[i]);
    assert(currWarp->warpState == UNINITIALIZED);
    assert(currWarp->dq_.front().second == -1);
    waitingWarps.push(currWarp);
  }

  // pop data from waitingWarps to
  for (int i = 0; i < std::min(activeWarps, MAXWARPS); i++) {
    warps_[i] = waitingWarps.front();
    assert(waitingWarps.front()->dq_.front().second == -1);
    assert(warps_[i]->dq_.front().second == -1);
    waitingWarps.pop();
    warp_t *currWarp = (warps_[i]);

    currWarp->warpState = RUNNABLE;

    // Initialize the register file to have all registers be ready.
    for (int reg = 0; reg < REGISTER_COUNT; reg++) {
      currWarp->rf_[reg] = {.regNum = reg, .ready = true};
    }
  }

  printf("instruction count: %d\n", instructionCount_);
  assert((warps_[0]->dq_).size() == instructionCount_);
  assert(warps_[0]->dq_.front().second == -1);

  // QUESTION: when we read all ops do we

  // initialize insturction queue of each warp

  // initialize stalling counter
  memTickDelayCounter = 0;
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
 * 4. currently implementing barriers
 * @return True if any instructions were successfully fetched.
 */

// TODO: I think this was meant to belong to the SM class? This needs to be
// double-checked.
std::pair<trace_op *, uint64_t> SM::scheduler() {
  bool isStalled = false;

  for (int i = 0; i < MAXWARPS; i++) {
    // std::cout << "Warp " << i << " state: " << warps_[i].warpState <<
    // std::endl;
    // skip unused warps
    if (warps_[i] == NULL)
      continue;

    if (warps_[i]->warpState == FINISHED ||
        warps_[i]->warpState == UNINITIALIZED) {
      continue;
    } else if (warps_[i]->warpState == STALLED) {
      isStalled =
          true; // at least 1 warp is stalling, so we are definitly not finished
      printf("warp : %d is in a barrier and can not execute\n", i);
      continue;
    } else if (warps_[i]->warpState == RUNNABLE) {
      if (warps_[i]->dq_.empty()) {
        // printf("queue empty!\n");
        continue;
      }
      //   int *x = NULL;
      //   *x = 1;
      // checks that there is no hazard
      // ASSUMES that rs1 and rs2 can not be 0
      // assert(warps_[0]->dq_.front().second == -1);
      auto [warp_next_instr, warp_id] = warps_[i]->dq_.front();

      // IDK: removed all tags and instead will generate them when scheduler
      // dispatches
      assert(warp_id == -1);

      // trace_op *warpNextInstr = warps_[i].dq_.front();

      int rs1 = warp_next_instr->src_reg[0];
      int rs2 = warp_next_instr->src_reg[1];

      if (rs1 != -1 && warps_[i]->rf_[rs1].ready == false) {
        std::cout << "Register rs1=" << rs1
                  << " is not ready, so the instruction (" << warp_next_instr
                  << ", " << i << ")"
                  << " is stalled" << std::endl;
        continue;
      } else if (rs2 != -1 && warps_[i]->rf_[rs2].ready == false) {
        std::cout << "Register rs2=" << rs2
                  << " is not ready, so the warp is stalled" << std::endl;
        continue;
      } else if (warp_next_instr->op == BARRIER) { // checks if it is a barrier
        // needs to wait until all instruction that update
        // architectural state retires
        bool skip = false;
        for (int regNum = 0; regNum < REGISTER_COUNT; regNum++) {
          if (warps_[i]->rf_[regNum].ready == false)
            skip = true;
        }
        // we can not issue barrier because one instruction is still in pipeline
        // that has not yet commited to architectural state/register files
        if (skip)
          continue;
        // get ready to issue the barrier instruction through the pipeline
        int selectedWarp = i;
        warps_[i]->dq_.pop_front();
        std::pair<trace_op *, uint64_t> returnPair;
        returnPair.first = warp_next_instr;
        returnPair.second = selectedWarp;

        stallCount_++;
        warps_[i]->warpState = STALLED;
        printf("stallCount: %d\n", stallCount_);
        // set all warps back to runnable if very thread met barrier
        if (stallCount_ == activeWarps_) {
          printf("wefijwefwef\n");
          stallCount_ = 0; // reset stallcount;
          // checks that all warps are stalled
          for (int i = 0; i < activeWarps_; i++) {
            assert(allWarps[i].warpState == STALLED);
          }
          // sets all warps to runnable
          for (int i = 0; i < activeWarps_; i++) {
            allWarps[i].warpState = RUNNABLE;
          }
          // assertions to check for correctness
          for (int i = 0; i < std::min(activeWarps_, MAXWARPS); i++) {
            assert(warps_[i]->warpState == RUNNABLE);
          }
          // return now so that you don't add yourself back to the waiting
          // queue;
          return returnPair;
        }

        // checks if there are any warp in the waiting queue we can grab
        // as we are stalling
        if (waitingWarps.size() > 0) {
          assert(warps_[i]->warpState == STALLED);
          waitingWarps.push(warps_[i]);
          warps_[i] = waitingWarps.front();
          waitingWarps.pop();
          warp_t *currWarp = (warps_[i]);
          if (currWarp->warpState == STALLED) {
            // check that all registers are not used
            for (int reg = 0; reg < REGISTER_COUNT; reg++) {
              assert(currWarp->rf_[reg].ready == true);
            }
          } else {
            // Initialize the register file to have all registers be ready.
            for (int reg = 0; reg < REGISTER_COUNT; reg++) {
              currWarp->rf_[reg] = {.regNum = reg, .ready = true};
            }
            printf("warpState: %d\n", currWarp->warpState);
            assert(currWarp->warpState != STALLED);
            currWarp->warpState = RUNNABLE;
          }
        }
        return returnPair;
      } else {
        // no register conflicts, can return
        int selectedWarp = i;

        // trace_op *warpInstr = warps_[i].dq_.pop();
        warps_[i]->dq_.pop_front(); // TODO: Check if this is right

        std::pair<trace_op *, uint64_t> returnPair;

        // TODO: Was this the intended instruction (Ethan: YES)
        returnPair.first = warp_next_instr;
        returnPair.second = selectedWarp;

        // TODO: put this in wb stage
        // this means that schedule should happen in fetch_falling and
        // wb should all do it's computation in rising
        if (warps_[i]->dq_.size() == 0) {
          warp_t *currWarp = (warps_[i]);
          std::cout << "warp: " << i << " has finished!" << std::endl;
          currWarp->warpState = FINISHED;
        }

        // schedule new warp if current warp is finished
        if (warps_[i]->warpState == FINISHED && waitingWarps.size() > 0) {
          warps_[i] = waitingWarps.front();
          waitingWarps.pop();
          warp_t *currWarp = (warps_[i]);

          // Initialize the register file to have all registers be ready.
          for (int reg = 0; reg < REGISTER_COUNT; reg++) {
            currWarp->rf_[reg] = {.regNum = reg, .ready = true};
          }

          assert(currWarp->warpState != STALLED);
          currWarp->warpState = RUNNABLE;
        }

        return returnPair;
      }
    }
  }

  // all warps are stalled
  std::cout << "All warps are stalled." << std::endl;
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
  bool progress = false;

  /*
      If the queue going into the "Decode" stage already has instructions in it,
      then the pipeline is stalled and we do not make progress.
  */

  //   if (!fetch_decode_queue_.empty()) {
  //     return progress;
  //   }

  std::pair<trace_op *, uint64_t> instrPair = scheduler();

  trace_op *currentInstruction = instrPair.first;
  uint64_t warpNumber = instrPair.second;

  std::cout << "Current Instruction"
            << " (Warp #" << warpNumber << ")"
            << ": " << currentInstruction << std::endl;

  // do not make progress if all warps are stalled
  if (currentInstruction == NULL && warpNumber == -1)
    return false;

  warp_t *scheduledWarp = (warps_[warpNumber]);

  // update register files

  // TODO: What was "I" meant to be?
  int dest = currentInstruction->dest_reg;

  // If the register is -1, it means no register is required.
  if (dest != -1) {
    printf("register %d is used", dest);
    scheduledWarp->rf_[dest].ready = false;
  }

  fetch_decode_queue_.push(instrPair);

  // If we got here, then we made progress
  return true;
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
  /*
      For now, just shunt the instruction along to the next stage.
  */
  bool progress = false;

  if (!decode_execute_queue_.empty()) {
    /*
        TODO: For now, we exit if the next stage is stalled and do not perform
        any additional logic. We may have to revisit this assumption later.
    */
    return progress;
  }

  if (fetch_decode_queue_.empty()) {
    // This phase did not do anything, since we have nothing to fetch
    return progress;
  }

  auto instrPair = fetch_decode_queue_.front();

  /*
    DANGER: Don't want to pop this unless the next phase can receive it. Note
    the "decode_execute_queue" guard above.
  */
  fetch_decode_queue_.pop();

  decode_execute_queue_.push(instrPair);

  // If we got here, then we made progress
  return true;
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
  /*
      TODO: For now, we just shunt the instruction along to the next phase
     again. Later, we'll want to introduce variable delays for instructions.
  */

  bool progress = false;

  if (!execute_mem_queue_.empty()) {
    /*
        TODO: For now, we return immediately if the next stage of the pipeline
        is stalled. We therefore perform no extra logic, so we might need to
        revisit this later.
    */
    return progress;
  }

  if (decode_execute_queue_.empty()) {
    /*
        TODO: If we have nothing to consume, then we did not make progress.
    */
    return progress;
  }

  auto instrPair = decode_execute_queue_.front();

  // DANGER: make sure instruction is not thrown away
  decode_execute_queue_.pop();
  execute_mem_queue_.push(instrPair);

  // If we got here, then we made progress
  return true;
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
  /*
      TODO: For now, we just shunt the instruction along to the next phase
     again. Later, we'll want to introduce delays, or integrate with various
     "memory" components.
  */
  bool progress = false;

  if (!mem1_mem2_queue_.empty()) {
    /*
        TODO: For now, we return immediately if the next stage of the pipeline
        is stalled. We therefore perform no extra logic, so we might need to
        revisit this later.
    */
    return progress;
  }

  if (execute_mem_queue_.empty()) {
    /*
        TODO: If we have nothing to consume, then we did not make progress.
    */
    return progress;
  }

  auto instrPair = execute_mem_queue_.front();
  auto [instr, warp_id] = instrPair;
  execute_mem_queue_.pop();

  if (instr != NULL && instr->op == MEM_LOAD) {
    int64_t tag = makeTag(warp_id, int32_t(instr->memAddress));
    mem_waiting_t wait;
    wait.warpID = warp_id;
    wait.instr = instr;
    wait.tag = tag;
    mem_waiting_vector.push_back(
        wait); // add this to the instructions awaiting memory
    cs_->memoryRequest(instr, 0, tag, memOpCallback_);
    return true;
  }

  // stall unless we meet the right delay
  // if (instr != NULL && instr->op == MEM_LOAD) {
  //   memTickDelayCounter++;
  //   if (memTickDelayCounter < 100) {
  //     std::cout << "stalling memory" << std::endl;
  //     return true;
  //   } else {
  //     memTickDelayCounter = 0;
  //   }
  // }

  // DANGER: make sure instruction is not thrown away
  // we allow non memory operations through
  mem1_mem2_queue_.push(instrPair);

  // If we got here, then we made progress
  return true;
}

/******************************************************************************
 Memory Falling
 ******************************************************************************/

/**
 * @brief the falling edge of the memory cycle
 *
 * @return unsure what the return types are
 */
bool SM::Mem_falling() {

  bool progress = false;
  printf("%d instructions waiting for memory \n", mem_waiting_vector.size());
  if (mem1_mem2_queue_.empty()) {
    // if no data is waiting for memory and no data is ready, then we done
    if (mem_ready_queue_.empty() && mem_waiting_vector.size() == 0)
      return false;
    else if (mem_ready_queue_.size() >
             0) { // there is memory that has finished waiting for data
      auto instrPair = mem_ready_queue_.front();
      mem_ready_queue_.pop();
      mem2_wb_queue_.push(instrPair);
      return true;
    } else { // there is data waiting for memory
      return true;
    }
  }

  // pipe non memory operations through
  auto instrPair = mem1_mem2_queue_.front();
  mem1_mem2_queue_.pop();
  mem2_wb_queue_.push(instrPair);
  return true;
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
  /*
    TODO: For now, we just mark the instruction as finished. We'll probably
    need to revisit this.
  */

  bool progress = false;

  // TODO: Is there anything that can stall this pipeline phase?

  if (mem2_wb_queue_.empty()) {
    /*
        TODO: If we have nothing to consume, then we did not make progress.
    */
    return progress;
  }

  auto [instr, warp_id] = mem2_wb_queue_.front();

  // DANGER: make sure instruction is not thrown away
  mem2_wb_queue_.pop();

  // TODO: Maybe need to push onto "finished instructions" (?)

  /*
      My understanding is that write back is where you would mark registers as
      ready.

      Idk about the "hazard" of identical source and destination registers
      (Source: https://en.wikipedia.org/wiki/Classic_RISC_pipeline)

      TODO: What about the destination register.
  */
  int dest = instr->dest_reg;
  if (dest != -1)
    warps_[warp_id]->rf_[dest].ready = true;

  // If we got here, then we made progress

  return true;
}

/******************************************************************************
 memory call back
 ******************************************************************************/

bool SM::handleMemOpCallback(int64_t tag) {

  for (int i = 0; i < mem_waiting_vector.size(); i++) {
    mem_waiting_t wait = mem_waiting_vector[i];
    if (wait.tag == tag) {
      std::pair<trace_op *, uint64_t> returnPair(wait.instr, wait.warpID);
      mem_ready_queue_.push(returnPair); // push the
      mem_waiting_vector.erase(mem_waiting_vector.begin() + i);
      printf("found data from memory\n");
      return true;
    }
  }

  // we must find the person who requested the memory data in
  // mem_waiting_vector;
  printf("should not happen\n");
  assert(false);
  return false;
}

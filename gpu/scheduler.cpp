#include "sm.h"
#include "trace.h"
#include <iostream>
#include <optional>
#include <stdexcept>
#include "scheduler.h"

Scheduler::Scheduler(SM *parent_sm) : parent_sm_(parent_sm) {
  std::cout << "New Scheduler Constructed." << std::endl;
  assert(parent_sm_ != NULL);

  instructions_ = parent_sm_->GetInstructions();
}

void Scheduler::InitWarp(int warp_id) {
    assert(warp_current_instruction_.find(warp_id) == warp_current_instruction_.end());
    assert(parent_sm_ != NULL);

    CFG *const cfg = parent_sm_->GetCFG();
    assert(cfg != NULL);

    /*
        Assume that we start executing from the beginning of the trace:
        - Current instruction index = 0
        - Push "dummy" entry onto the reconvergence stack, because divergence
        code relies on modifiying an existing entry on the stack.
    */
    
    warp_current_instruction_[warp_id] = 0;
    reconv_stack_entry_t entry;
    entry.RetReconvPC = std::nullopt;
    
    std::string bb_name = cfg->GetLineBasicBlockName(0);
    entry.NextPC = bb_name;

    warp_current_bb_[warp_id] = bb_name;
    
    // All active to begin with
    entry.ActiveMask = std::vector<bool>(THREADSPERWARP, true);

    // Push to stack
    warp_reconv_stacks_[warp_id] = std::stack<reconv_stack_entry_t>();
    warp_reconv_stacks_[warp_id].push(entry);
}

sm_instruction_t *Scheduler::GetNextInstruction() {
  std::cout << std::endl << "Scheduler::GetNextInstruction()" << std::endl;
  for (int i = 0; i < MAXWARPS; i++) {
    int warp_id = i;
    warp_t *const warp_ptr = parent_sm_->GetWarpPointer(i);

    std::cout << "\tWarp " << i << " state: " << warp_ptr->warpState << std::endl;
    if (warp_ptr->warpState == FINISHED ||
        warp_ptr->warpState == UNINITIALIZED) {
      std::cout << "\t\tstate is finished or uninitialized" << std::endl;
      continue;
    } else if (warp_ptr->warpState == STALLED_MEMORY || warp_ptr->has_active_control_hazard) {
      std::cout << "\t\tstate is stalled ";
      if (warp_ptr->warpState == STALLED_MEMORY)  {
        std::cout << "(memory)";
      } else if (warp_ptr->has_active_control_hazard) {
        std::cout << "(control)";
      }

      std::cout << std::endl;

      continue;
    } else if (warp_ptr->warpState == RUNNABLE ||
               warp_ptr->warpState == RUNNING) {

      /*
         Initialize internal state for this warp if it does not exist.

         TODO: Maybe it would be nice to factor out this initialization logic
         into a separate helper.
      */
      if (warp_current_instruction_.find(warp_id) ==
          warp_current_instruction_.end()) {
        InitWarp(warp_id);
      }

      int current_instruction_id = warp_current_instruction_[warp_id];
      assert(0 <= current_instruction_id &&
             current_instruction_id < instructions_.size());

      trace_op *warp_next_instr = instructions_[current_instruction_id];

      std::cout << "\tNext instruction (" << current_instruction_id
                << ") would be " << (warp_next_instr->op) << std::endl;

      /*
            Check if we have reached a reconvergence point
      */
      auto &S = warp_reconv_stacks_[warp_id];

      if (S.empty()) {
        warp_ptr->warpState = FINISHED;
        std::cout << "Warp ID " << warp_id << " is finished" << std::endl;
        continue;
      }

      CFG *cfg = parent_sm_->GetCFG();
      auto new_bb = cfg->GetLineBasicBlockName(current_instruction_id);

      assert(!S.empty());
      // assert(S.top().RetReconvPC.has_value());

      if (S.top().RetReconvPC.has_value() && S.top().RetReconvPC.value() == new_bb) {
        std::string old_reconv_point = S.top().RetReconvPC.value();
        while (true) {
          if (S.empty()) {
            warp_ptr->warpState = FINISHED;
            return GetNextInstruction();
          }

          S.pop();
          assert(!S.empty());

          // Go to nextpc on stack, and replace active mask
          auto top_entry = S.top();
          // S.pop();

          auto new_line_no = cfg->GetBasicBlockLineNo(top_entry.NextPC);
          warp_current_instruction_[warp_id] = new_line_no;
          warp_current_bb_[warp_id] = top_entry.NextPC;

          bool hasActive = false;
          for (int tid = 0; tid < THREADSPERWARP; tid++) {
            bool val =
                top_entry.ActiveMask[tid] && (!warp_ptr->finished_mask[tid]);
            warp_ptr->active_mask[tid] = val;
            hasActive = hasActive || val;
          }

          if (hasActive) {
            return GetNextInstruction();
          }
        }
      }

      /*
          Check that all registers are ready

          TODO: Is WAW a problem?
      */
      bool all_sources_ready = true;

      for (int source_idx = 0; source_idx < warp_next_instr->num_sources;
           source_idx++) {
        // Check that the source is a register
        operand_t source = warp_next_instr->sources[source_idx];
        if (source.op_kind != REGISTER) {
          continue;
        }

        // Check that the register is ready
        std::string register_name = source.register_name;
        if (!warp_ptr->rf_[register_name].ready_) {
          all_sources_ready = false;

          std::cout << "\t\tRegister " << register_name
                    << " is not ready, so the instruction (" << warp_next_instr
                    << ")"
                    << "is stalled" << std::endl;
          break;
        }
      }

      if (all_sources_ready) {
        // no register conflicts, can return
        int selectedWarp = i;

        // trace_op *warpInstr = warps_[i].dq_.pop();
        warp_current_instruction_[warp_id]++;

        std::pair<trace_op *, uint64_t> returnPair;

        // TODO: Was this the intended instruction (Ethan: YES)
        returnPair.first = warp_next_instr;
        returnPair.second = selectedWarp;

        sm_instruction_t *new_sm_instruction = new sm_instruction_t;
        new_sm_instruction->instruction_idx = current_instruction_id;
        new_sm_instruction->warp_id = warp_id;
        new_sm_instruction->t_op = warp_next_instr;

        // TODO: put this in wb stage
        // this means that schedule should happen in fetch_falling and
        // wb should all do it's computation in rising
        // if (warp_current_instruction_[warp_id] >= instructions_.size()) {
        //   std::cout << "\t\twarp id: " << i << " has finished!" << std::endl;
        //   warp_ptr->warpState = FINISHED;
        // }

        return new_sm_instruction;
      }
    }
  }
  // all warps are stalled
  std::cout << "All warps are stalled." << std::endl;

  return NULL;
}

void Scheduler::NotifyBranch(int instruction_idx, int warp_id, std::optional<std::vector<bool>> predicate) {
  std::cout << "Scheduler::NotifyBranch(instr_idx=" << instruction_idx
            << ", warp_id=" << warp_id << ")" << std::endl;

  /*
      Algo:
      TODO: Fill this in
  */

  warp_t *const warp_ptr = parent_sm_->GetWarpPointer(warp_id);

  if (!predicate.has_value()) {
    // Just make it the active lane mask
    predicate = std::vector<bool>(THREADSPERWARP);
    
    for (int tid = 0; tid < THREADSPERWARP; tid++) {
      (*predicate)[tid] = warp_ptr->active_mask[tid] && (!warp_ptr->finished_mask[tid]);
    }
  }

  auto &S = warp_reconv_stacks_[warp_id];
  assert(!S.empty());
  auto top_entry = S.top();

  auto instr = instructions_[instruction_idx];

  CFG *cfg = parent_sm_->GetCFG();

  auto curr_bb = cfg->GetLineBasicBlockName(instruction_idx);

  auto successors = cfg->GetSuccessors(curr_bb);

  if (instr->op == RET) {
    // Mark active threads as finished
    std::cout << "\tContemplating RET\n";
    bool allFinished = true;
    for (int tid = 0; tid < THREADSPERWARP; tid++) {
      if ((*predicate)[tid]) {
        warp_ptr->finished_mask[tid] = true;
      }

      allFinished = allFinished && warp_ptr->finished_mask[tid];
    }

    if (allFinished) {
      warp_ptr->warpState = FINISHED;
      std::cout << "All finished at S.size() = " << S.size() << std::endl;
      return;
    }
  } else if (!successors.empty()) {
    S.pop();
    std::string reconv_point = cfg->GetIPDom(curr_bb);
    top_entry.NextPC = reconv_point;
    S.push(top_entry);

    for (auto [pred_opt, succ_bb] : successors) {
      if (pred_opt.has_value()) {
        // This is the "if" case, take the predicate to be the active mask
        reconv_stack_entry_t if_entry;
        if_entry.RetReconvPC = reconv_point;
        if_entry.NextPC = succ_bb;

        // Active mask assumed to be already correctly contructed in predicate
        if_entry.ActiveMask = *predicate;

        // Just in case, combine with finished mask
        for (int tid = 0; tid < THREADSPERWARP; tid++) {
          if_entry.ActiveMask[tid] = if_entry.ActiveMask[tid] && (!warp_ptr->finished_mask[tid]);
        }

        // But we need to check if it should actually be executed
        bool atLeastOneThread = false;
        for (int tid = 0; tid < THREADSPERWARP; tid++) {
          atLeastOneThread |= if_entry.ActiveMask[tid];
        }

        if (atLeastOneThread) {
          S.push(if_entry);
        }
      } else {
        /*
            Else case - I *think* the active lane mask is the negation of
            the predicate anded with the current active lane mask
        */

        std::vector<bool> new_mask(THREADSPERWARP);

        bool atLeastOneThread = false;
        for (int tid = 0; tid < THREADSPERWARP; tid++) {
          bool val = (!predicate.value()[tid]) && warp_ptr->active_mask[tid] && (!warp_ptr->finished_mask[tid]);
          new_mask[tid] = val;

          atLeastOneThread |= val;
        }

        reconv_stack_entry_t else_entry;
        else_entry.RetReconvPC = reconv_point;
        else_entry.NextPC = succ_bb;
        else_entry.ActiveMask = new_mask;

        if (atLeastOneThread) {
          S.push(else_entry);
        }
      }
    }
  } 

  // if (S.empty()) {
  //   warp_ptr->warpState = FINISHED;
  //   return;
  // }

  assert(!S.empty());

  while (true) {
    if (S.empty()) {
      warp_ptr->warpState = FINISHED;
      return;
    }

    // From this point, continue execution from the NextPC of the *new* top
    // entry
    auto new_top_entry = S.top();
    auto next_bb = new_top_entry.NextPC;
    auto new_line_no = cfg->GetBasicBlockLineNo(next_bb);

    // Also replace the active mask
    bool hasActive = false;
    for (int tid = 0; tid < THREADSPERWARP; tid++) {
      bool val =
          new_top_entry.ActiveMask[tid] && (!warp_ptr->finished_mask[tid]);
      warp_ptr->active_mask[tid] = val;
      hasActive = hasActive || val;
    }

    if (hasActive) {
      std::cout << "\tWarp " << warp_id << " branching " << curr_bb << " (line "
                << instruction_idx << ")"
                << " -> " << next_bb << " (line " << new_line_no << ")"
                << std::endl;

      std::cout << "\tNew mask: ";
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        std::cout << warp_ptr->active_mask[tid];
      }
      std::cout << std::endl;

      warp_current_instruction_[warp_id] = new_line_no;
      return;
    } else {
      S.pop();
    }
    // warp_current_bb_[warp_id] = next_bb;
  }
}

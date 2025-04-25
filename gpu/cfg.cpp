#include "cfg.h"
#include "trace.h"
#include <iostream>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>


CFG::CFG(std::vector<trace_op *> instructions) : instructions_(instructions) {
  BuildCFG();
  ComputePostDominators();
  ComputeImmediatePostDominators();
}

void CFG::BuildCFG() {
    std::cout << std::endl << "CFG::BuildCFG()" << std::endl;
    int fresh_label = 0;
    std::optional<std::string> curr_label = std::nullopt;
    
    // Map predecessor to successor line id
    std::vector<std::pair<std::string, int>> unresolved_successors;

    // Map line number to basic block name
    std::unordered_map<int, std::string> line_to_block;
    
    for (int instruction_idx = 0; instruction_idx < instructions_.size(); instruction_idx++) {
        // std::cout << "instruction_idx: " << instruction_idx << std::endl;
        trace_op *curr_instr = instructions_[instruction_idx];

        if (!curr_label.has_value()) {
            if (curr_instr->op != LABEL) {
                curr_label = "fresh_label_" + std::to_string(fresh_label++);
            } else {
                assert(curr_instr->dest_reg != NULL);
                curr_label = curr_instr->dest_reg;
            }

            basic_block_line_nos_[*curr_label] = instruction_idx;
        } else {
            // We suddenly run into a new label
            if (curr_instr->op == LABEL) {
                assert(curr_instr->dest_reg != NULL);
                std::string new_label = curr_instr->dest_reg;

                // Update successor info
                if (successors_.find(*curr_label) == successors_.end()) {
                    successors_[*curr_label] = {{std::nullopt, new_label}};
                } else {
                    successors_[*curr_label].emplace_back(std::nullopt, new_label);
                }

                // Set up the new label
                curr_label = new_label;
                basic_block_line_nos_[*curr_label] = instruction_idx;
            }
        }

        // std::cout << "Line " << instruction_idx << " -> " << *curr_label << std::endl;
        line_to_block[instruction_idx] = *curr_label;

        if (curr_instr->op != BRA) {
            continue;
        }

        std::optional<std::string> predicate = {};
        if (curr_instr->guard_reg != NULL) {
            predicate = curr_instr->guard_reg;
        }

        if (successors_.find(*curr_label) != successors_.end()) {
            successors_[*curr_label].emplace_back(predicate, curr_instr->dest_reg);
        } else {
            successors_[*curr_label] = {{predicate, curr_instr->dest_reg}};
        }

        if (instruction_idx + 1 < instructions_.size()) {
            trace_op *next_instr = instructions_[instruction_idx + 1];
            if (next_instr->op != BRA || next_instr->variant != BRA_UNI) {
                // Write successor, and begin a new basic block
                if (curr_instr->variant != BRA_UNI) {
                    unresolved_successors.emplace_back(*curr_label, instruction_idx + 1);
                }

                curr_label = {};
                continue;
            }

            // Allow the next instruction (a BRA_UNI) to deal with the rest
            continue;
        }
    }

    for (auto [curr_block, successor_line_no] : unresolved_successors) {
        assert(line_to_block.find(successor_line_no) != line_to_block.end());
        assert(successors_.find(curr_block) != successors_.end());

        std::string successor_block_name = line_to_block[successor_line_no];
        successors_[curr_block].emplace_back(std::nullopt, successor_block_name);
    }

    line_basic_block_names_ = line_to_block;

    // The below is purely for debugging purposes
    std::string curr_basic_block_name = "";
    for (int instruction_idx = 0; instruction_idx < instructions_.size(); instruction_idx++) {
        std::string basic_block_name = line_to_block[instruction_idx];

        if (basic_block_name != curr_basic_block_name) {
          curr_basic_block_name = basic_block_name;
          std::cout << "BASIC BLOCK: " << curr_basic_block_name << std::endl;

          if (successors_.find(curr_basic_block_name) != successors_.end()) {
            for (auto [pred, successor] : successors_[curr_basic_block_name]) {
              std::cout << "\tSuccessor: ";

              if (pred.has_value()) {
                std::cout << *pred << " ";
              }

              std::cout << successor << std::endl;
            }
          }
        }

        trace_op *instr = instructions_[instruction_idx];
        switch (instr->op) {
        case LABEL: {
            std::cout << "LABEL" << std::endl;
            break;
        }
        case LDPARAM: {
            std::cout << "LDPARAM" << std::endl;
            break;
        }
        case MOV: {
            std::cout << "MOV" << std::endl;
            break;
        }
        case MUL: {
            std::cout << "MUL" << std::endl;
            break;
        }
        case SETP: {
            std::cout << "SETP" << std::endl;
            break;
        }
        case BRA: {
            std::cout << "BRA" << std::endl;
            break;
        }
        case CVTA: {
            std::cout << "CVTA" << std::endl;
            break;
        }
        case ADD: {
            std::cout << "ADD" << std::endl;
            break;
        }
        case SUB: {
            std::cout << "SUB" << std::endl;
            break;
        }
        case SHR: {
            std::cout << "SHR" << std::endl;
            break;
        }
        case LD: {
            std::cout << "LD" << std::endl;
            break;
        }
        case ST: {
            std::cout << "ST" << std::endl;
            break;
        }
        case RET:
            std::cout << "RET" << std::endl;
            break;
        }
    }
}

void CFG::ComputePostDominators() {
  std::cout << std::endl << "CFG::ComputePostDominators()" << std::endl;
  /*
      Implemented from pseudocode given in slides from UMich:
      https://web.eecs.umich.edu/~mahlke/courses/483f06/lectures/483L20.pdf

      "Given some BB, which blocks are guaranteed to have executed after
      executing the BB"
  */

  std::unordered_map<std::string, std::set<std::string>> pdom;

  /*
      Initialize pdoms to empty sets.
  */
  std::vector<std::string> bb_names;
  for (auto &[basic_block_name, curr_bb_line_no] : basic_block_line_nos_) {
    pdom[basic_block_name] = std::set<std::string>(); // Empty set
    bb_names.push_back(basic_block_name);
  }

  /*
      Figure out exit BB
  */
  std::optional<std::string> exit_bb;
  for (int instr_idx = 0; instr_idx < instructions_.size(); instr_idx++) {
    trace_op *instr = instructions_[instr_idx];

    if (instr->op == RET) {
      assert(!exit_bb.has_value());
      exit_bb = line_basic_block_names_[instr_idx];
    }
  }
  
  assert(exit_bb.has_value());

  /*
        Initialization:
        - pdom(exit) = exit
        - pdom(everything else) = all nodes
  */
  pdom[*exit_bb] = {*exit_bb};
  for (auto bb_name : bb_names) {
    if (bb_name == *exit_bb) {
        continue;
    }

    for (auto other_bb_name : bb_names) {
        pdom[bb_name].insert(other_bb_name);
    }
  }


  bool change = true;
  
  int iteration = 0;

  while (change) {
    change = false;
    std::cout << "Iteration " << iteration++ << std::endl;

    std::unordered_map<std::string, std::set<std::string>> tmp_pdom = pdom;

    for (auto &[basic_block_name, _] : basic_block_line_nos_) {
        if (basic_block_name == *exit_bb) {
            continue;
        }
        std::cout << "\tBB: " << basic_block_name << std::endl;

        std::cout << "\t\tCurrent pdom[BB]: ";
        for (auto pd : pdom[basic_block_name]) {
            std::cout << pd << " ";
        }

        std::cout << std::endl;

        std::set<std::string> tmp_bb = {basic_block_name};
        std::vector<std::pair<std::optional<std::string>, std::string>> successors = {};
        if (successors_.find(basic_block_name) != successors_.end()) {
            successors = successors_[basic_block_name];
        }

        bool first = true;
        std::set<std::string> intersect_successor_pdoms = {};
        std::cout << "\t\tSuccessors:" << std::endl;
        for (std::pair<std::optional<std::string>, std::string> p : successors) {
            std::cout << "\t\t\t" << p.second << std::endl;
            std::string successor_bb_name = p.second;
            if (first) {
                intersect_successor_pdoms = pdom[successor_bb_name];
                first = false;
                continue;
            }

            std::set<std::string> tmp = {};
            for (auto curr_member : intersect_successor_pdoms) {
                if (pdom[successor_bb_name].find(curr_member) != pdom[successor_bb_name].end()) {
                    tmp.insert(curr_member);
                }
            }

            intersect_successor_pdoms = tmp;
        }

        std::cout << "\t\tNew Members:" << std::endl;
        for (auto pd : intersect_successor_pdoms) {
            std::cout << "\t\t\t" << pd << std::endl;
            tmp_bb.insert(pd);
        }

        if (tmp_bb != pdom[basic_block_name]) {
            tmp_pdom[basic_block_name] = tmp_bb;
            change = true;
        }
    }

    pdom = tmp_pdom;
  }

  pdoms_ = pdom;

  // Debugging
  std::cout << std::endl;
  std::optional<std::string> curr_bb = std::nullopt;
  for (int instr_idx = 0; instr_idx < instructions_.size(); instr_idx++) {
    std::string bb_name = line_basic_block_names_[instr_idx];
    if (!curr_bb.has_value() || bb_name != *curr_bb) {
        curr_bb = bb_name;
    } else {
        continue;
    }

    std::cout << "BASIC BLOCK: " << bb_name << std::endl;
    for (auto pd : pdom[bb_name]) {
        std::cout << "\tpdom: " << pd << std::endl;
    }
  }
}

void CFG::ComputeImmediatePostDominator(std::string node_name) {
    /*
        Do a BFS starting from the node. The first node that post-dominates this
        one is our result.
    */

    // If nothing post-dominates us, then there is nothing to do
    if (pdoms_.find(node_name) == pdoms_.end()) {
        return;
    }

    auto &pds = pdoms_[node_name];
    std::queue<std::string> Q;
    Q.push(node_name);

    while (!Q.empty()) {
        std::string curr_node_name = Q.front();
        Q.pop();

        if (node_name != curr_node_name && pds.find(curr_node_name) != pds.end()) {
            // This is our result!
            ipdoms_[node_name] = curr_node_name;
            break;
        }

        // Add all successors to Q
        if (successors_.find(curr_node_name) == successors_.end()) {
            continue;
        }

        for (auto [_, succ] : successors_[curr_node_name]) {
            Q.push(succ);
        }
    }
}

void CFG::ComputeImmediatePostDominators() {
  std::cout << std::endl << "CFG::ComputeImmediatePostDominators()" << std::endl;
  /*
      An immediate post-dominator of a node is the first breadth-first
      successor of a node that post dominates it:
      https://web.eecs.umich.edu/~mahlke/courses/483f06/lectures/483L20.pdf

      Before calling this function, ComputePostDominators should be called.
  */

  for (auto [node_name, _] : basic_block_line_nos_) {
    ComputeImmediatePostDominator(node_name);
  }

  // Debugging
  std::cout << std::endl;
  std::optional<std::string> curr_bb = std::nullopt;
  for (int instr_idx = 0; instr_idx < instructions_.size(); instr_idx++) {
    std::string bb_name = line_basic_block_names_[instr_idx];
    if (!curr_bb.has_value() || bb_name != *curr_bb) {
      curr_bb = bb_name;
    } else {
      continue;
    }

    std::cout << "BASIC BLOCK: " << bb_name << std::endl;
    std::cout << "\tipdom: ";

    if (ipdoms_.find(bb_name) != ipdoms_.end()) {
        std::cout << ipdoms_[bb_name];
    } else {
        std::cout << "none";
    }

    std::cout << std::endl;
  }
}
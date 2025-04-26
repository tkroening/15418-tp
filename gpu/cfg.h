#ifndef CFG_H
#define CFG_H

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
extern "C" {
    #include "trace.h"
}

#include <vector>

class CFG {
    public:
        CFG(std::vector<trace_op*> instructions);

        std::string GetLineBasicBlockName(int instruction_idx) {
            assert(line_basic_block_names_.find(instruction_idx) != line_basic_block_names_.end());
            return line_basic_block_names_[instruction_idx];
        }

        int GetBasicBlockLineNo(std::string bb_name) {
            return basic_block_line_nos_[bb_name];
        }

        std::vector<std::pair<std::optional<std::string>, std::string>> GetSuccessors(std::string bb_name) {
            if (successors_.find(bb_name) == successors_.end()) {
                return {};
            }

            return successors_[bb_name];
        }

        std::string GetIPDom(std::string bb_name) {
            return ipdoms_[bb_name];
        }

    private:
        void BuildCFG();

        // Post-Dominators
        void ComputePostDominators();
        
        // Immediate Post-Dominators
        void ComputeImmediatePostDominator(std::string node_name); // Helper for *one* node
        void ComputeImmediatePostDominators(); // Compute for all nodes

        std::vector<trace_op *> instructions_;
        
        // Zero-indexed to match vector
        std::unordered_map<std::string, int> basic_block_line_nos_;
        std::unordered_map<int, std::string> line_basic_block_names_;

        /* 
            Directed forward edges in CFG.

            Maps:
                std::string - Representing the name of the current basic block
            To a vector of:
                std::pair<std::optional<std::string>, std::string>
            where the first element in the pair is an optional string
            corresponding to the name of a predicate reigster, and the second
            element is the name of the successor basic block.
         */
        std::unordered_map<std::string, std::vector<std::pair<std::optional<std::string>, std::string>>> successors_;

        /*
            Post-Dominators

            - A post dominator for a node x is any node that is guaranteed to
            execute after x, which includes itself.
        */
        std::unordered_map<std::string, std::set<std::string>> pdoms_;

        // Immediate post-dominators - unique ipdom per node
        std::unordered_map<std::string, std::string> ipdoms_;
};

#endif
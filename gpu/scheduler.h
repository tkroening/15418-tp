/*
    Warp scheduler
*/
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "sm.h"
#include <stack>
#include <unordered_map>

// Forward declaration
class SM;
typedef struct _sm_instruction_t sm_instruction_t;

/*
    Type for entries in the reconvergence stack
*/
typedef struct _reconvergence_stack_entry_t {
    std::optional<std::string> RetReconvPC;
    std::string NextPC; // Label of next basic block to go to
    std::vector<bool> ActiveMask;
} reconv_stack_entry_t;

typedef struct _naive_stack_entry_t {
    std::string NextPC; // Label of next basic block to go to
    std::vector<bool> ActiveMask;
} naive_stack_entry_t;

class Scheduler {
    public:
        explicit Scheduler (SM *parent_sm) : parent_sm_(parent_sm) {}
        virtual ~Scheduler() = default;

        // Get the next instruction to execute - could be from any warp
        virtual sm_instruction_t *GetNextInstruction();

        // Public function to notify scheduler that a branch has occurred so that it can update its state
        virtual void NotifyBranch(int instruction_idx, int warp_id, std::optional<std::vector<bool>> predicate);

    protected:
        SM *parent_sm_;
};

class ReconvergenceScheduler : public Scheduler {
    public:
        explicit ReconvergenceScheduler(SM *parent_sm);
        virtual ~ReconvergenceScheduler() override = default;
        
        // Get the next instruction to execute - could be from any warp
        sm_instruction_t *GetNextInstruction() override;

        // Public function to notify scheduler that a branch has occurred so that it can update its state
        void NotifyBranch(int instruction_idx, int warp_id, std::optional<std::vector<bool>> predicate) override;
    private:
        // Our own copy of instructions
        std::vector<trace_op *> instructions_;

        /*
            Initialize internal state for a specific warp
        */
        void InitWarp(int warp_id);

        /*
            Map warp_id to the zero-based integer index representing the
            warp's current instruction.
        */
        std::unordered_map<int, int> warp_current_instruction_;
        
        /*
            Used to detect fall-through (sudden change in BB without branch)

            warp_id : int -> bb_name : str
        */
        std::unordered_map<int, std::string> warp_current_bb_;

        /*
            Track active mask for each warp

            warp_id : int -> lane mask : vector<bool>
        */
        std::unordered_map<int, std::vector<bool>> warp_active_masks_;

        /*
            Also track a reconvergence stack for each warp

            warp id -> stack
        */
        std::unordered_map<int, std::stack<reconv_stack_entry_t>> warp_reconv_stacks_;
};

class NaiveScheduler : public Scheduler {
    public:
        explicit NaiveScheduler(SM *parent_sm);
        virtual ~NaiveScheduler() override = default;
        
        // Get the next instruction to execute - could be from any warp
        sm_instruction_t *GetNextInstruction() override;

        // Public function to notify scheduler that a branch has occurred so that it can update its state
        void NotifyBranch(int instruction_idx, int warp_id, std::optional<std::vector<bool>> predicate) override;
    private:
        // Our own copy of instructions
        std::vector<trace_op *> instructions_;

        /*
            Initialize internal state for a specific warp
        */
        void InitWarp(int warp_id);

        /*
            Map warp_id to the zero-based integer index representing the
            warp's current instruction.
        */
        std::unordered_map<int, int> warp_current_instruction_;
        

        /*
            Track active mask for each warp

            warp_id : int -> lane mask : vector<bool>
        */
        std::unordered_map<int, std::vector<bool>> warp_active_masks_;

        /*
            Also track a reconvergence stack for each warp

            warp id -> stack
        */
        std::unordered_map<int, std::stack<naive_stack_entry_t>> warp_continuation_stacks_;
};

#endif
#include "sm.h"
#include "trace.h"

#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <stdio.h>
#include <sys/types.h>
#include <variant>

#include "cfg.h"
#include "scheduler.h"

std::string op_to_string(op_type op_t) {
  switch (op_t) {
    case LABEL : { return "LABEL"; }
    case ADD : { return "ADD"; }
    case LDPARAM : { return "LDPARAM"; }
    case MOV : { return "MOV"; }
    case MUL : { return "MUL"; }
    case SETP : { return "SETP"; }
    case BRA : { return "BRA"; }
    case CVTA : { return "CVTA"; }
    case SUB: { return "SUB"; }
    case SHR: {return "SHR"; }
    case LD: { return "LD"; }
    case ST: { return "ST"; }
    case RET: { return "RET"; }
  }
}

// Constructor
SM::SM(void (*memOpCallback)(int, int64_t), ProcessorArgs args, processor *self,
       trace_reader *tr, int activeWarps, int smid)
    : memOpCallback_(memOpCallback), args_(args), ps_(self), tr_(tr), smid_(smid) {

  instructionCount_ = 0;
  // initialize registers and state of each warp
  for (int i = 0; i < MAXWARPS; i++) {
    warp_t *currWarp = &(warps_[i]);

    // only intialize active warps
    if (i < activeWarps) {
      // all warps are initially runnable
      currWarp->warpState = RUNNING;

      // No control hazard initially
      currWarp->has_active_control_hazard = false;

      // Initialize the register file to have all registers be ready.
      for (int reg_idx = 0; reg_idx < tr->num_registers; reg_idx++) {
        std::string register_name = tr->register_names[reg_idx];
        std::cout << "Register name: " << register_name << std::endl;
        currWarp->rf_[register_name] = Register();
        currWarp->rf_[register_name].register_name_ = register_name;
        currWarp->rf_[register_name].ready_ = true;
      }

      /*
          Initialize parameter registers

          TODO: Presumably this makes more sense to do in the GPU itself
          rather than individual SMs.
      */
      for (int reg_idx = 0; reg_idx < tr->num_registers; reg_idx++) {
        std::vector<Value> register_values;
        param_t *param = tr->getParamValue(tr->register_names[reg_idx]);
        if (param == NULL) {
          continue;
        }

        std::cout << "Register: " << tr->register_names[reg_idx] << std::endl;

        if (param->is_pointer) {
          // TODO: The reinterpret cast seems very dangerous
          std::cout << "\tExtracted pointer param " << param->param_pointer << std::endl;
          register_values = std::vector<Value>(THREADSPERWARP, reinterpret_cast<uint64_t>(param->param_pointer));
        } else {
          switch (param->primitive_type) {
            case TR_PRIMITIVE_INT : {
              std::cout << "\tExtracted param " << param->param_int << std::endl;
              register_values = std::vector<Value>(THREADSPERWARP, param->param_int);
              break;
            }
            case TR_PRIMITIVE_FLOAT : {
              std::cout << "\tExtracted param " << param->param_float << std::endl;
              register_values = std::vector<Value>(THREADSPERWARP, param->param_float);
              break;
            }
          }
        }

        std::string register_name = tr->register_names[reg_idx];
        for (int tid = 0; tid < THREADSPERWARP; tid++) {
          currWarp->rf_[register_name].register_values_[tid] = register_values[tid];
        }


      }

      /*
          Set up CUDA variables:
          - CUDA's "threadIdx.x" is PTX "%tid.x".
          - CUDA's "blockIdx.x" is PTX "%ctaid.x".
          - CUDA's "blockDim.x" is PTX "%ntid.x".

          Source: https://www.cs.uaf.edu/2011/spring/cs641/lecture/03_03_CUDA_PTX.html

          TODO: Again, this probably ought to be moved somewhere else or
          assigned more cleverly in the future. The current values are
          hardcoded!!
      */
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        currWarp->rf_["\%ctaid.x"].register_values_[tid] = 0;
        currWarp->rf_["\%ntid.x"].register_values_[tid] = 32;
        currWarp->rf_["\%tid.x"].register_values_[tid] = tid;
      }

      // Lane mask is assumed to be 100% active initially
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        currWarp->active_mask[tid] = true;
        currWarp->finished_mask[tid] = false;
      }

    } else {
      currWarp->warpState = UNINITIALIZED;
    }
  }

  /** @brief moves all ops onto instruction queue of warps */
  trace_op *op;

  /*
      TODO: This needs to be taken out and moved somewhere proper.
  */
  std::vector<trace_op *> instrs;

  while (true) {
    /*
        TODO: For now, the implementation can only handle one SM because
        the calls to getNextOp eventually exhaust the internal "iterator".

        The logic that ingests all the trace_op's should therefore probably
        be moved to the owning "GPU" class rather than having it run for
        repeatedly for each SM.
    */
    op = tr_->getNextOp(0);
    // if we reach end of trace file we break out of loop
    if (op == NULL) {
      std::cout << "Finished reading tracefile (hit NULL). Read "
                << instructionCount_ << " instructions." << std::endl;
      break;
    } else {
      instructionCount_++;
      instrs.push_back(op);
      assert(instrs.size() == instructionCount_);
    }
  }

  instructions_ = instrs;
  cfg_ = new CFG(instrs);
  scheduler_ = new Scheduler(this);
}

warp_t *SM::GetWarpPointer(int warp_id) {
  return &(warps_[warp_id]);
}

std::vector<trace_op *> SM::GetInstructions() {
  return instructions_;
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

  if (!fetch_decode_queue_.empty()) {
    return progress;
  }

  sm_instruction_t *sm_instr = scheduler_->GetNextInstruction();
  if (sm_instr == NULL) {
    return false;
  }

  trace_op *currentInstruction = sm_instr->t_op;
  uint64_t warpNumber = sm_instr->warp_id;

  std::cout << "Current Instruction"
            << " (Warp #" << warpNumber << ")"
            << ": " << currentInstruction << " "
            << "[" << op_to_string(currentInstruction->op) << "]" << std::endl;

  warp_t *scheduledWarp = &(warps_[warpNumber]);

  /*
      Scoreboarding: make destination as unavailable
  */
  if (currentInstruction->dest_reg != NULL) {
    std::string register_name = currentInstruction->dest_reg;

    scheduledWarp->rf_[register_name].ready_ = false;
  }

  /*
      Mark control hazard

      TODO: Should this be done in Fetch? Can it be moved elsewhere? The danger
      is that it is not correct to put subsequent instructions in the pipeline.
  */
  if (currentInstruction->op == BRA || currentInstruction->op == RET) {
    /*
        Control hazard - don't want to issue any more instructions from this
        warp.
    */
    warps_[warpNumber].has_active_control_hazard = true;
  }

  fetch_decode_queue_.push(sm_instr);

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

  if (!mem_wb_queue_.empty()) {
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

  // DANGER: make sure instruction is not thrown away
  execute_mem_queue_.pop();
  mem_wb_queue_.push(instrPair);

  // If we got here, then we made progress
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

  if (mem_wb_queue_.empty()) {
    /*
        TODO: If we have nothing to consume, then we did not make progress.
    */
    return progress;
  }

  auto sm_instr = mem_wb_queue_.front();
  auto instr = sm_instr->t_op;
  auto warp_id = sm_instr->warp_id;

  // DANGER: make sure instruction is not thrown away
  mem_wb_queue_.pop();

  DoComputation(sm_instr);

  // TODO: Maybe need to push onto "finished instructions" (?)

  /*
      My understanding is that write back is where you would mark registers as
      ready.

      Idk about the "hazard" of identical source and destination registers
      (Source: https://en.wikipedia.org/wiki/Classic_RISC_pipeline)

      TODO: What about the destination register.
  */
  if (instr->dest_reg != NULL) {
    std::string register_name = instr->dest_reg;
    warps_[warp_id].rf_[register_name].ready_ = true;
  }

  // If we got here, then we made progress

  return true;
}

/******************************************************************************
 Computation
 ******************************************************************************/

/*
    Helper for the "wide" variant of instructions. From the PTX ISA docs:

    mul.wide.s32 z,x,y;        // 32*32 bits, creates 64 bit result
*/
op_width getDoubleWidth(op_width old_width) {
  switch (old_width) {
    case S32 : {
      return S64;
    }
    default : {
      throw std::runtime_error("getDoubleWidth() unimplemented for this width");
    }
  }
}

/*
    Helper to convert values
*/
Value convertValue(Value oldValue, op_width targetSize) {
  Value result = std::visit([targetSize](auto &v) -> Value {
    switch (targetSize) {
      case U32 : {
        return static_cast<uint32_t>(v);
      }
      case F32 : {
        return static_cast<float>(v);
      }
      case U64 : {
        return static_cast<uint64_t>(v);
      }
      case S32 : {
        return static_cast<int>(v);
      }
      case S64 : {
        return static_cast<int64_t>(v);
      }
      case OP_WIDTH_NONE : {
        throw std::runtime_error("Unsupported conversion width");
      }
    }
  }, oldValue);

  return result;
}

bool extractBoolFromValue(Value v) {
  bool result = std::visit([](auto &v_prime) -> bool {
    return static_cast<bool>(v_prime);
  }, v);

  return result;
}

template <typename BinOp>
Value convertAndApplyBinop(trace_op *instr, Value a, Value b, BinOp binop) {
  return std::visit(
      [b, instr, binop](auto &v_a) -> Value {
        return std::visit(
            [instr, binop, v_a](auto &v_b) -> Value {
              switch (instr->width) {
                case S32 : {
                  return binop(
                    static_cast<int32_t>(v_a),
                    static_cast<int32_t>(v_b)
                  );
                }
                case S64 : {
                  return binop(
                    static_cast<int64_t>(v_a),
                    static_cast<int64_t>(v_b)
                  );
                }
                case F32 : {
                  return binop(
                    static_cast<float>(v_a),
                    static_cast<float>(v_b)
                  );
                }
                default : {
                  throw std::runtime_error("Unsupported binop width");
                }
              } 
            },
            b);
      },
      a);
}

std::vector<Value> SM::GetValueFromSource(operand_t *src, uint64_t warp_id) {
  switch (src->op_kind) {
    case IMMEDIATE_INT : {
      return std::vector<Value>(THREADSPERWARP, src->immediate_int);
    }
    case IMMEDIATE_FLOAT : {
      return std::vector<Value>(THREADSPERWARP, src->immediate_float);
    }
    case REGISTER : {
      // Try to fetch register
      assert(src->register_name != NULL);
      std::string register_name = src->register_name;
      assert(warps_[warp_id].rf_.find(register_name) != warps_[warp_id].rf_.end());

      std::vector<Value> result;
      for (auto v : warps_[warp_id].rf_[register_name].register_values_) {
        result.push_back(v);
      }

      return result;
    }
  }
}

/*
    Return the value stored at the address represented by memValue
*/

template<typename ValueType>
Value loadValueFromPointer(trace_op *instr, ValueType ptr) {
  switch (instr->width) {
    case F32 : {
      return *reinterpret_cast<float *>(ptr);
    }
    case U32 : {
      return *reinterpret_cast<uint32_t *>(ptr);
    }
    default : {
      throw std::runtime_error("loadValueFromPointer(): Unsupported pointer type");
    }
  }
}

Value loadValue(trace_op *instr, Value memValue) {
  if (std::holds_alternative<int64_t>(memValue)) {
    auto addr = std::get<int64_t>(memValue);
    return loadValueFromPointer(instr, addr);
  } else if (std::holds_alternative<uint64_t>(memValue)) {
    auto addr = std::get<uint64_t>(memValue);
    return loadValueFromPointer(instr, addr);
  } else {
    throw std::runtime_error("loadValue() called with something that can't be made into a pointer.");
  }
}

template<typename PtrType, typename ValueType>
void storeValueIntoPointer(trace_op *instr, PtrType ptr, ValueType src) {
  switch (instr->width) {
    case F32 : {
      float *new_ptr = reinterpret_cast<float *>(ptr);
      *new_ptr = src;
      break;
    }
    case U32 : {
      uint32_t *new_ptr = reinterpret_cast<uint32_t *>(ptr);
      *new_ptr = src;
      break;
    }
    default : {
      throw std::runtime_error("storeValueIntoPointer(): Unsupported width");
    }
  }
}

template<typename ValueType>
void storeValueHelper(trace_op *instr, Value destValue, ValueType srcValue) {
  if (std::holds_alternative<int64_t>(destValue)) {
    auto addr = std::get<int64_t>(destValue);
    storeValueIntoPointer(instr, addr, srcValue);
  } else if (std::holds_alternative<uint64_t>(destValue)) {
    auto addr = std::get<uint64_t>(destValue);
    storeValueIntoPointer(instr, addr, srcValue);
  } else {
    throw std::runtime_error("storeValueHelper() called with something that can't be made into a pointer.");
  }
}

void storeValue(trace_op* instr, Value destValue, Value srcValue) {
  std::visit([instr, destValue](auto &v) -> void {
    switch (instr->width) {
      case F32 : {
        return storeValueHelper(
          instr,
          destValue,
          static_cast<float>(v)
        );
      }
      case U32 : {
        return storeValueHelper(
          instr,
          destValue,
          static_cast<uint32_t>(v)
        );
      }
      default : {
        throw std::runtime_error("Unsupported src type for storeValue");
      }
    }
  }, srcValue);
}

bool VariantIsIntegral(Value v) {
  return std::holds_alternative<int32_t>(v) ||
  std::holds_alternative<uint32_t>(v) ||
  std::holds_alternative<int64_t>(v) ||
  std::holds_alternative<uint64_t>(v);
}



void SM::DoComputation(sm_instruction_t *sm_instr) {
  auto instr = sm_instr->t_op;
  auto instr_idx = sm_instr->instruction_idx;
  auto warp_id = sm_instr->warp_id;

  std::cout << "SM::DoComputation(" << op_to_string(instr->op) << ") ";
  for (int tid = 0; tid < THREADSPERWARP; tid++) {
    std::cout << warps_[warp_id].active_mask[tid];
  }

  std::cout << std::endl;


  // Want to fetch all of the "source" values
  std::vector<std::vector<Value>> source_values;
  for (int source_idx = 0; source_idx < instr->num_sources; source_idx++) {
    auto source = instr->sources[source_idx];
    source_values.push_back(GetValueFromSource(&source, warp_id));
  }

  switch (instr->op) {
    /*
        Note here that we are treating a lot of instructions (LDPARAM, MOV, CVTA)
        as being exactly the same. We may need to revisit this assumption at
        some point.
    */
    case LDPARAM :
    case MOV :
    case CVTA : {
      assert(source_values.size() == 1);
      std::vector<Value> result(THREADSPERWARP);

      /*
        Just cast everything in the source to the desired width
      */
      assert(instr->width != OP_WIDTH_NONE);

      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        result[tid] = convertValue(source_values[0][tid], instr->width);
      }

      // Write back result
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }
        warps_[warp_id].rf_[instr->dest_reg].register_values_[tid] = result[tid];
      }

      break;
    }
    case MUL : {
      assert(source_values.size() == 2);
      std::vector<Value> result(THREADSPERWARP);

      /*
        Cast the two operands to the desired width
      */
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        Value a = source_values[0][tid];
        Value b = source_values[1][tid];

        switch (instr->variant) {
          case OP_VARIANT_NONE : {
            result[tid] = convertAndApplyBinop(
                instr, a, b, [](auto v_a, auto v_b) { return v_a * v_b; });

            break;
          }
          case MUL_WIDE : {
            trace_op temp_trace_op;
            temp_trace_op = *instr;
            temp_trace_op.width = getDoubleWidth(instr->width);

            result[tid] = convertAndApplyBinop(
                &temp_trace_op, a, b, [](auto v_a, auto v_b) { return v_a * v_b; });

            break;
          }
          default : {
            throw std::runtime_error("Unsupported variant for MUL");
          }
        }

      }

      // Write back
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        warps_[warp_id].rf_[instr->dest_reg].register_values_[tid] = result[tid];
      }

      break;
    }
    case ADD : {
      assert(source_values.size() == 2);
      std::vector<Value> result(THREADSPERWARP);

      /*
          Cast and do the add
      */
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        Value a = source_values[0][tid];
        Value b = source_values[1][tid];

        result[tid] = convertAndApplyBinop(instr, a, b, 
          [](auto v_a, auto v_b) {
            return v_a + v_b;
          }
        );
      }

      // Write back
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        warps_[warp_id].rf_[instr->dest_reg].register_values_[tid] = result[tid];
      }

      break;
    }
    case SUB : {
      assert(source_values.size() == 2);
      std::vector<Value> result(THREADSPERWARP);

      /*
          Cast and do the subtraction
      */
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        Value a = source_values[0][tid];
        Value b = source_values[1][tid];

        result[tid] = convertAndApplyBinop(instr, a, b, 
          [](auto v_a, auto v_b) {
            return v_a - v_b;
          }
        );
      }

      // Write back
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        warps_[warp_id].rf_[instr->dest_reg].register_values_[tid] = result[tid];
      }

      break;
    }
    case SHR : {
      assert(source_values.size() == 2);
      std::vector<Value> result(THREADSPERWARP);

      /*
          Cast and do the shift
      */
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        Value a = source_values[0][tid];
        Value b = source_values[1][tid];

        /*
          TODO: Might be good to refactor into a "convertAndApplyIntegerBinop"
          function.
        */

        result[tid] = std::visit(
            [b, instr](auto &v_a) -> Value {
              return std::visit(
                  [instr, v_a](auto &v_b) -> Value {
                    switch (instr->width) {
                    case S32: {
                      return static_cast<int32_t>(v_a) >>
                             static_cast<int32_t>(v_b);
                    }
                    case S64: {
                      return static_cast<int64_t>(v_a) >>
                             static_cast<int64_t>(v_b);
                    }
                    case U32 : {
                      return static_cast<uint32_t>(v_a) >>
                             static_cast<uint32_t>(v_b);
                    }
                    default: {
                      throw std::runtime_error("Unsupported binop width");
                    }
                    }
                  },
                  b);
            },
            a);
      }

      // Write back
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        warps_[warp_id].rf_[instr->dest_reg].register_values_[tid] = result[tid];
      }

      break;
    }
    case SETP : {
      // TODO: We might need to revisit this assumption about the number of sources
      assert(source_values.size() == 2);
      
      // Need to set predicate registers
      std::vector<Value> result(THREADSPERWARP);

      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        Value a = source_values[0][tid];
        Value b = source_values[1][tid];

        switch (instr->variant) {
          case SETP_GE : {

            result[tid] = convertAndApplyBinop(instr, a, b,
                [](auto v_a, auto v_b) {
                  return v_a >= v_b;
                }
            );

            break;
          }
          case SETP_LT : {

            result[tid] = convertAndApplyBinop(instr, a, b,
                [](auto v_a, auto v_b) {
                  return v_a < v_b;
                }
            );

            break;
          }
          case SETP_GT : {
            result[tid] = convertAndApplyBinop(instr, a, b,
                [](auto v_a, auto v_b) {
                  return v_a > v_b;
                }
            );

            break;
          }
          default : {
            throw std::runtime_error("Unsupported SETP variant");
          }
        }
      }

      // Write back
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        warps_[warp_id].rf_[instr->dest_reg].register_values_[tid] = result[tid];
      }

      break;
    }
    case BRA : {
      std::vector<bool> predicate(THREADSPERWARP);
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        predicate[tid] = warps_[warp_id].active_mask[tid] && (!warps_[warp_id].finished_mask[tid]);
      }

      // Check for a guard register
      if (instr->guard_reg != NULL) {
        operand_t pred_register_operand;
        pred_register_operand.op_kind = REGISTER;
        pred_register_operand.register_name = instr->guard_reg;

        std::vector<Value> predicate_values = GetValueFromSource(&pred_register_operand, warp_id);
        std::vector<bool> new_mask = predicate;
        for (int tid = 0; tid < THREADSPERWARP; tid++) {
          bool reg_value = extractBoolFromValue(predicate_values[tid]);
          new_mask[tid] = reg_value && predicate[tid];
        }

        scheduler_->NotifyBranch(instr_idx, warp_id, new_mask);
      } else {
        // If there is no guard, then it ought to be a uniform branch
        assert(instr->variant == BRA_UNI);
        
        // Scheduler should use whatever the current mask is
        scheduler_->NotifyBranch(instr_idx, warp_id, std::nullopt);
      }

      // Clear the control hazard flag
      warps_[warp_id].has_active_control_hazard = false;

      break;
    }
    case LD : {
      assert(source_values.size() == 1);
      std::vector<Value> result(THREADSPERWARP);

      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        Value memValue = source_values[0][tid];
        result[tid] = loadValue(instr, memValue);
      }

      // Write back
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        warps_[warp_id].rf_[instr->dest_reg].register_values_[tid] = result[tid];
      }

      break;
    }
    case ST : {
      assert(source_values.size() == 1);

      // Note: this instruction has no "result"

      /*
          We need to know the destinations
      */

      // Hack: implement with a fake operand
      operand_t temp_operand;
      temp_operand.op_kind = REGISTER;
      temp_operand.register_name = instr->dest_reg;

      std::vector<Value> destination_addresses = GetValueFromSource(&temp_operand, warp_id);

      // Do the actual store
      for (int tid = 0; tid < THREADSPERWARP; tid++) {
        if (!warps_[warp_id].active_mask[tid]) { continue; }

        Value destValue = destination_addresses[tid];
        Value srcValue = source_values[0][tid];
        storeValue(instr, destValue, srcValue);
      }

      // No WB, since this instruction doesn't have a result
      break;
    }
    case LABEL : {
      // Do nothing
      break;
    }
    case RET : {
      scheduler_->NotifyBranch(instr_idx, warp_id, std::nullopt);

      // Clear control hazard
      warps_[warp_id].has_active_control_hazard = false;

      break;
    }
    default : {
      throw std::runtime_error("This instruction has not yet been implemented.");
    }
  }
}
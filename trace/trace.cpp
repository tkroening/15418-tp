#include "trace_reader.h"
#include <cctype>
#include <stdexcept>
#include <vector>

extern "C" {
    #include "trace.h"
}

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cassert>

TraceReader *tracereader;

// Prototype
extern "C" trace_op *getNextOp(int);

TraceReader::TraceReader(trace_sim_args *tsa) : trace_reader_state_(TR_STATE_READING_PARAMS) {
    std::cout << "Trace Reader constructor" << std::endl;
}

param_t getPrimitiveParamFromString(TraceReaderPrimitiveType primitive_type, std::string param_str) {
  switch (primitive_type) {
    case TR_PRIMITIVE_INT: {
        int val = std::stoi(param_str);
        
        return { .param_int = val };
    }
    case TR_PRIMITIVE_FLOAT : {
        float val = std::stof(param_str);

        return { .param_float = val };
    }
  }
}

template <typename T>
void *buildParamVector(std::vector<std::string> &param_value_strs) {
    T *result_arr = (T *) malloc(sizeof(T) * param_value_strs.size());

    for (int param_idx = 0; param_idx < param_value_strs.size(); param_idx++) {
        T val; 
        std::stringstream(param_value_strs[param_idx]) >> val;

        result_arr[param_idx] = val;
    }

    return (void *) result_arr;
}

void TraceReader::ReadParamLine(std::string line) {
    if (line == "---") {
        trace_reader_state_ = TR_STATE_READING_PTX;
        return;
    }

    // This is what we want out of the line
    std::string type_name;
    std::string param_name;
    std::vector<std::string> param_values;

    std::stringstream ss(line);
    std::string temp;
    while (std::getline(ss, temp, ' ')) {
        if (type_name.empty()) {
            type_name = temp;
            continue;
        }

        if (param_name.empty()) {
            param_name = temp;
            continue;
        }

        param_values.push_back(temp);
    }

    TraceReaderPrimitiveType primitive_type;
    if (type_name.starts_with("int")) {
        primitive_type = TR_PRIMITIVE_INT;
    } else if (type_name.starts_with("float")) {
        primitive_type = TR_PRIMITIVE_FLOAT;
    } else {
        throw std::runtime_error("Trace reader: unsupported primitive type for param. Line: " + line + "Extracted type_name: " + type_name);
    }

    bool isPointer = type_name.ends_with("*");
    
    if (!isPointer && param_values.size() == 1) {
        param_t val = getPrimitiveParamFromString(primitive_type, param_values[0]);
        param_map_[param_name] = val;
    } else if (isPointer && (param_values.size() >= 1)) {
        void *result_ptr = NULL;
        switch (primitive_type) {
            case TR_PRIMITIVE_INT : {
                result_ptr = buildParamVector<int>(param_values);
                break;
            }
            case TR_PRIMITIVE_FLOAT : {
                result_ptr = buildParamVector<float>(param_values);
                break;
            }
        }

        param_map_[param_name] = { .param_pointer = result_ptr };
    } else {
        throw std::runtime_error("Unsupported parameter format: " + line);
    }

    std::cout << "Inserting param name: " << param_name << std::endl;
    register_names_.insert(param_name);
}

op_space parseOpSpace(std::string op_space_str) {
    if (op_space_str == "global") {
        return SPACE_GLOBAL;
    }

    throw std::runtime_error("Could not parse op space: " + op_space_str);
}

op_width parseOpWidth(std::string op_width_str) {
    if (op_width_str == "u32") {
        return U32;
    } else if (op_width_str == "f32") {
        return F32;
    } else if (op_width_str == "u64") {
        return U64;
    } else if (op_width_str == "s32") { 
        return S32;
    } else if (op_width_str == "s64") {
        return S64;
    }

    throw std::runtime_error("Could not parse op width: " + op_width_str);
}

char *make_char_array(std::string s) {
    char *new_arr = (char *) malloc(sizeof(char) * (s.size() + 1));

    for (int char_idx = 0; char_idx < s.size(); char_idx++) {
        new_arr[char_idx] = s[char_idx];
    }

    // NUL Terminator
    new_arr[s.size()] = 0;

    return new_arr;
}

void parseOperatorInfo(trace_op *new_trace_op, std::string operator_info_string) {
    std::string op_str;
    std::vector<std::string> modifier_strs;

    std::stringstream info_ss = std::stringstream(operator_info_string);
    std::string temp;
    while (std::getline(info_ss, temp, '.')) {
        std::cout << "info temp: " << temp << std::endl;
        if (op_str.empty()) {
            op_str = temp;
            continue;
        }

        modifier_strs.push_back(temp);
    }

    if (op_str.empty()) {
        throw std::runtime_error("No operator provided: " + operator_info_string);
    }

    if (op_str == "ldparam") {
        new_trace_op->op = LDPARAM;

        // Must have size. Should not include anything extra
        assert(modifier_strs.size() == 1);
        new_trace_op->width = parseOpWidth(modifier_strs[0]);
    } else if (op_str == "mov") {
        new_trace_op->op = MOV;

        // Must have size?
        assert(modifier_strs.size() == 1);
        new_trace_op->width = parseOpWidth(modifier_strs[0]);
    } else if (op_str == "mul") {
        new_trace_op->op = MUL;
        assert(modifier_strs.size() >= 1);

        if (modifier_strs.size() == 2) {
            new_trace_op->variant = MUL_WIDE; // TODO: Parsing function
            new_trace_op->width = parseOpWidth(modifier_strs[1]);
        } else if (modifier_strs.size() == 1) {
            new_trace_op->width = parseOpWidth(modifier_strs[0]);
        } else {
            throw std::runtime_error("Malformed instruction: " + operator_info_string);
        }
    } else if (op_str == "add") {
        new_trace_op->op = ADD;
        assert(modifier_strs.size() == 1);

        new_trace_op->width = parseOpWidth(modifier_strs[0]);
    } else if (op_str == "setp") {
        new_trace_op->op = SETP;
        assert(modifier_strs.size() == 2);

        // Parse the comparator
        if (modifier_strs[0] == "ge") {
            new_trace_op->variant = SETP_GE;
        } else {
            throw std::runtime_error("Unsupported variant of setp: " + modifier_strs[0]);
        }

        new_trace_op->width = parseOpWidth(modifier_strs[1]);
    } else if (op_str == "bra") {
        new_trace_op->op = BRA;
        
        // TODO: Branch variants are *not* supported for now
        assert(modifier_strs.size() == 0);
    } else if (op_str == "cvta") {
        new_trace_op->op = CVTA;
        assert(modifier_strs.size() == 2);

        // Should have both a space and a size
        new_trace_op->space = parseOpSpace(modifier_strs[0]);
        new_trace_op->width = parseOpWidth(modifier_strs[1]);
    } else if (op_str == "ld") {
        new_trace_op->op = LD;
        assert(modifier_strs.size() == 2);

        // Should have both a space and a size
        new_trace_op->space = parseOpSpace(modifier_strs[0]);
        new_trace_op->width = parseOpWidth(modifier_strs[1]);
    } else if (op_str == "st") {
        new_trace_op->op = ST;
        assert(modifier_strs.size() == 2);

        // Should have both a space and a size
        new_trace_op->space = parseOpSpace(modifier_strs[0]);
        new_trace_op->width = parseOpWidth(modifier_strs[1]);
    } else if (op_str == "ret") {
        new_trace_op->op = RET;
        assert(modifier_strs.size() == 0);
    } else {
        throw std::runtime_error("Could not parse operator info: " + operator_info_string);
    }
}

operand_t parseOperand(std::set<std::string> &register_names, std::string operand_str) {
    try {
        long parsed_int = std::stol(operand_str);

        return {
            .op_kind = IMMEDIATE_INT,
            .immediate_int = parsed_int
        };
    } catch (std::out_of_range const &ex) {
        throw std::runtime_error("Parsed operand out of range: " + operand_str);
    } catch (...) {}

    try {
        double parsed_float = std::stod(operand_str);

        return {
            .op_kind = IMMEDIATE_FLOAT,
            .immediate_float = parsed_float
        };
    } catch (std::out_of_range const &ex) {
        throw std::runtime_error("Parsed operand out of range: " + operand_str);
    } catch (...) {}

    // Treat as register name
    if (!operand_str.starts_with("%")) {
        std::cout << "WARNING: Interpreted '" << operand_str << "' as register name" << std::endl;
    }

    // Log register name
    std::cout << "Logging register name " << operand_str << std::endl;
    register_names.insert(operand_str);

    return {
        .op_kind = REGISTER,
        .register_name = make_char_array(operand_str)
    };
}

/*
    NOTE: This function is intended to parse things of the format, then
    apply the extracted information onto new_trace_op by mutating it.

    operator.mod1.mod2 <...operands>
*/
void parseInstruction(std::set<std::string> &register_names, trace_op *new_trace_op, std::string line) {
    std::cout << std::endl << "parseInstruction(" << line << ")" << std::endl;
    std::string operator_info_string; // operator.mod1.mod2 -> e.g. st.global.f32
    std::vector<std::string> operand_strings;

    std::stringstream whole_ss = std::stringstream(line);
    std::string temp;
    while (std::getline(whole_ss, temp, ' ')) {
        std::cout << "temp:" << temp << std::endl;
        if (operator_info_string.empty()) {
            operator_info_string = temp;
            continue;
        }

        operand_strings.push_back(temp);
    }

    parseOperatorInfo(new_trace_op, operator_info_string);

    std::vector<operand_t> parsed_operands;

    switch (new_trace_op->op) {
        case LDPARAM :
        case MOV :
        case CVTA :
        case ST :
        case LD : {
            assert(operand_strings.size() == 2);
            new_trace_op->dest_reg = make_char_array(operand_strings[0]);
            parsed_operands.push_back(
                parseOperand(register_names, operand_strings[1])
            );

            break;
        }
        case MUL :
        case ADD :
        case SETP : {
            assert(operand_strings.size() == 3);
            new_trace_op->dest_reg = make_char_array(operand_strings[0]);

            parsed_operands.push_back(
                parseOperand(register_names, operand_strings[1])
            );

            parsed_operands.push_back(
                parseOperand(register_names, operand_strings[2])
            );

            break;
        }
        case BRA : {
            /*
                NOTE: Special logic for BRA. We will put the label name in dest_reg
            */

            assert(operand_strings.size() == 1);
            new_trace_op->dest_reg = make_char_array(operand_strings[0]);

            if (!operand_strings[0].starts_with("$")) {
                throw std::runtime_error("'" + operand_strings[0] + "' given as label. Are you sure?");
            }

            break;
        }
        case RET : {
            assert(operand_strings.size() == 0);
            break;
        }
        default : {
            throw std::runtime_error("Operator parsed but unsupported: " + line);
        }
    }

    operand_t *heap_operands = (operand_t *) malloc(sizeof(operand_t) * parsed_operands.size());
    for (int operand_idx = 0; operand_idx < parsed_operands.size(); operand_idx++) {
        heap_operands[operand_idx] = parsed_operands[operand_idx];
    }

    new_trace_op->num_sources = parsed_operands.size();
    new_trace_op->sources = heap_operands;
}

void TraceReader::ReadPTXLine(std::string line) {
    trace_op *new_trace_op = new trace_op;
    
    // Init - make sure these are all properly set by the end of this function!
    new_trace_op->space = SPACE_NONE;
    new_trace_op->variant = OP_VARIANT_NONE;
    new_trace_op->width = OP_WIDTH_NONE;

    new_trace_op->dest_reg = NULL; // char*
    
    new_trace_op->num_sources = 0;
    new_trace_op->sources = NULL; // operand*

    new_trace_op->guard_reg = NULL; // char*

    // TODO: Check for label
    // TODO: Check for guarded line
    if (line.starts_with("@")) {
        size_t space_idx = line.find(" ");
        if (space_idx == std::string::npos) {
            throw std::runtime_error("Malformed guarded line");
        }

        std::string guard_reg = line.substr(1, space_idx - 1);
        new_trace_op->guard_reg = make_char_array(guard_reg);
        
        line = line.substr(space_idx + 1, line.size() - space_idx);
    } else if (line.starts_with("$")) {
        /*
            NOTE: We'll just reuse dest_reg to hold the label name
        */
        new_trace_op->op = LABEL;
        new_trace_op->dest_reg = make_char_array(line.substr(0, line.size() - 1));
        std::cout << "New dest reg: " << new_trace_op->dest_reg << std::endl;

        trace_ops_.push_back(new_trace_op);
        return;
    }

    parseInstruction(register_names_, new_trace_op, line);
    trace_ops_.push_back(new_trace_op);

    // Log information about registers
    if (new_trace_op->dest_reg != NULL) {
        register_names_.insert(new_trace_op->dest_reg);
    }
}

bool lineEmpty(std::string line) {
    for (auto c : line) {
        if (!isspace(c)) {
            return false;
        }
    }

    return true;
}

void TraceReader::ReadLine(std::string line) {
    if (lineEmpty(line)) {
        std::cout << "Line was empty: " << line << std::endl;
        return;
    }

    switch (trace_reader_state_) {
        case TR_STATE_READING_PARAMS : {
            ReadParamLine(line);
            break;
        }
        case TR_STATE_READING_PTX : {
            ReadPTXLine(line);
            break;
        }
    }
}

std::pair<int, std::vector<std::string>> TraceReader::GetRegisterInfo() {
    std::vector<std::string> result;
    for (auto s : register_names_) {
        result.push_back(s);
    }

    return {
        result.size(),
        result
    };
}

trace_op* TraceReader::GetNextOp() {
    if (trace_op_ctr_ >= trace_ops_.size()) {
        return NULL;
    }

    return trace_ops_[trace_op_ctr_++];
}

extern "C" trace_reader *init(trace_sim_args *tsa)
{
    trace_reader *tr = new trace_reader;

    tracereader = new TraceReader(tsa);

    std::string traceFileName;

    int op = 0;
    while ((op = getopt(tsa->arg_count, tsa->arg_list, "hdvc:p:o:n:i:b:t:s:m:")) != -1)
    {
        switch (op)
        {
            case 't':
                traceFileName = optarg;
                break;
        }
    }

    std::ifstream file(traceFileName);
    std::string line;

    if (file.is_open()) {
        while (std::getline(file, line)) {
            tracereader->ReadLine(line);
        }

        file.close();
    } else {
        throw std::runtime_error("Couldn't open the specified trace file");
    }

    auto [num_registers, register_names] = tracereader->GetRegisterInfo();

    /*
        Bind all the relevant fields in the trace reader interface
    */
    tr->num_registers = num_registers;
    tr->register_names = (char **) malloc(sizeof(char*) * num_registers);

    for (int reg_idx = 0; reg_idx < num_registers; reg_idx++) {
        tr->register_names[reg_idx] = make_char_array(register_names[reg_idx]);
    }

    tr->getNextOp = getNextOp;

    return tr;
}

extern "C" trace_op *getNextOp(int proc_id) {
    if (tracereader == NULL) {
        throw std::runtime_error("getNextOp() called, but tracereader is uninitialized.");
    }

    return tracereader->GetNextOp();
}

extern "C" int tick() {
    return 1;
}

extern "C" int finish(int outFd) {
    return 0;
}

extern "C" int destroy(void) {
    // TODO: Fill this in
    return 0;
}
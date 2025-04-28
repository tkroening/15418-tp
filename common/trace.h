/*
    Trace: Public Interface to Trace File Reader

    Author: Theo Kroening <tkroenin@andrew.cmu.edu>
    Based on a design by bpr
*/

#ifndef TRACE_H
#define TRACE_H

#include "common.h"

enum TraceReaderPrimitiveType
{
    TR_PRIMITIVE_INT,
    TR_PRIMITIVE_FLOAT
};

typedef struct param_t_ {
    TraceReaderPrimitiveType primitive_type;
    bool is_pointer;
    union {
        int param_int;
        float param_float;
        void *param_pointer;
    };
} param_t;

enum op_type
{
    LABEL,
    LDPARAM,
    MOV,
    MUL,
    SETP,
    BRA,
    CVTA,
    ADD,
    SUB,
    SHR,
    LD,
    ST,
    RET,
    AND,
    XOR,
    NOT
};

enum op_width 
{
    OP_WIDTH_NONE, // Undefined
    U32,
    F32,
    U64,
    S32,
    S64,
    B32,
    PRED // Like a boolean?
};

enum op_space
{
    SPACE_NONE, // Undefined
    SPACE_GLOBAL,
    SPACE_SHARED
};

enum op_variant
{
    OP_VARIANT_NONE, // Undefined - does not apply
    MUL_WIDE,
    SETP_GE,
    SETP_LT,
    SETP_GT,
    SETP_NE,
    SETP_EQ,
    BRA_UNI
};

typedef int proc_id;

enum operand_kind
{
    REGISTER,
    IMMEDIATE_INT,
    IMMEDIATE_FLOAT
};

typedef struct _operand {
    operand_kind op_kind;
    union {
        char *register_name;
        long immediate_int;
        double immediate_float;
    };
} operand_t;

typedef struct _trace_op {
    op_type op;
    op_space space;
    op_variant variant;
    op_width width;

    char *dest_reg;

    int num_sources;
    operand_t *sources;

    // Optional guard register - e.g. @%p1 bra $L__BB0_2
    char *guard_reg;
} trace_op;


typedef struct _trace_sim_args {
    int arg_count;
    char** arg_list;
} trace_sim_args;

// C-style interface for the stuff exposed by the trace reader
typedef struct _trace_reader {
    sim_interface si;
    trace_op* (*getNextOp)(int);
    param_t* (*getParamValue)(char*);

    int num_registers;
    char **register_names;
} trace_reader;

#endif

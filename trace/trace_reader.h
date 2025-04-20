#include <string>
#include <unordered_map>
#include <vector>
extern "C" {
    #include "trace.h"
}

enum TraceReaderState 
{
    TR_STATE_READING_PARAMS,
    TR_STATE_READING_PTX
};

enum TraceReaderPrimitiveType
{
    TR_PRIMITIVE_INT,
    TR_PRIMITIVE_FLOAT
};

class TraceReader {
    public:
        // Constructor
        TraceReader(trace_sim_args *tsa);

        // Methods
        void ReadLine(std::string line);

        trace_op *getNextOp();

        /*
            Fields
        */

        TraceReaderState trace_reader_state_;
        std::vector<trace_op*> trace_ops_;
        std::unordered_map<std::string, param_t> param_map_;

    private:
        void ReadParamLine(std::string line);
        void ReadPTXLine(std::string line);
};
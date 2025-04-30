"""
PTX Transpiler

Translates limited subset of PTX into a simpler syntax. Also breaks down fused
instructions into simpler, separated instructions.

Authors:
- Ethan Lu <eblu@andrew.cmu.edu>
- Theo Kroening <tkroenin@andrew.cmu.edu>

Input and Output file-names are command line arguments, for example:
python3 transpiler.py ptx_saxpy.txt saxpy.txt
"""

import argparse
from enum import Enum

class TranspilerState(Enum):
    PROGRAM_PARAMS = 1
    PTX = 2


# Signature for a translation
class Translation:
    def __init__(self, translations, outputFile):
        self.translations = translations
        self.outputFile = outputFile

    def match(self, line : str, currentState: TranspilerState) -> bool:
        return False

    def transform(self, currentState : TranspilerState, line : str) -> tuple[TranspilerState, str]:
        return currentState, ""

class Comment(Translation):
    def match(self, line : str, currentState: TranspilerState) -> bool:
        return line.startswith("//")

    # No transform - throw these away

class Whitespace(Translation):
    def match(self, line, currentState):
        return line.strip() == ""

class Param(Translation):
    def match(self, line, currentState) -> bool:
        return currentState == TranspilerState.PROGRAM_PARAMS

    def transform(self, currentState: TranspilerState, line: str):
        # Start translating PTX
        if line.strip() == "---":
            return TranspilerState.PTX, line + "\n"

        return currentState, line + "\n"

class LDParam(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.strip().startswith("ld.param")

    def transform(self, currentState: TranspilerState, line: str):
        operator, register, *_ = line.split()
        register = register[:-1]

        startIdx = line.find("_param_")
        assert(startIdx >= 0)

        paramName = line[startIdx:]
        endIdx = paramName.find("]")
        assert(endIdx >= 0)
        paramName = paramName[:endIdx]

        size = operator.split(".")[2]

        output = f"ldparam.{size} {register} {paramName}\n"
        return currentState, output

class Mov(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("mov.")

    def transform(self, currentState: TranspilerState, line: str):
        operator, dest, src = line.split()
        dest = dest[:-1]
        src = src[:-1]

        output = f"{operator} {dest} {src}\n"

        return currentState, output

class Setp(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("setp")

    def transform(self, currentState: TranspilerState, line: str):
        operator, pred, src1, src2 = line.split()

        pred = pred[:-1]
        src1 = src1[:-1]
        src2 = src2[:-1]

        op, cond, size = operator.split(".")

        output = f"{op}.{cond}.{size} {pred} {src1} {src2}\n"
        return currentState, output

class Mad(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("mad")

    def transform(self, currentState: TranspilerState, line: str):
        operator, d, a, b, c = line.split()
        d = d[:-1]
        a = a[:-1]
        b = b[:-1]
        c = c[:-1]

        op, variant, size = operator.split(".")
        assert(variant == "lo")

        output = f"mul.{size} {d} {a} {b}\n"
        output += f"add.{size} {d} {d} {c}\n"
        
        return currentState, output

class Fma(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("fma.")

    def transform(self, currentState: TranspilerState, line: str):
        operator, d, a, b, c = line.split()
        d = d[:-1]
        a = a[:-1]
        b = b[:-1]
        c = c[:-1]

        op, variant, size = operator.split(".")
        print(f"WARNING: fma used with rounding mode {variant}")

        output = f"mul.{size} {d} {a} {b}\n"
        output += f"add.{size} {d} {d} {c}\n"
        
        return currentState, output

class Guarded(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("@")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        splitIdx = line.find(" ")
        guard = line[:splitIdx]
        rest = line[splitIdx + 1:].strip()
        
        _, output = processLine(self.translations, outputFile, rest, currentState)
        return currentState, f"{guard} {output}"

class Branch(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("bra")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        # WARNING: bra.uni handled the same as bra
        operator, label = line.split()
        label = label[:-1]

        if operator.strip() == "bra":
            return currentState, f"bra {label}\n"
        else:
            op, variant = operator.split(".")
            return currentState, f"{op}.{variant} {label}\n"

class CVTA(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("cvta.")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        operator, dest, src = line.split()

        dest = dest[:-1]
        src = src[:-1]
        op, _, space, size = operator.split(".")

        return currentState, f"{op}.{space}.{size} {dest} {src}\n"

class Mul(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("mul.")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        operator, dest, src1, src2 = line.split()
        dest = dest[:-1]
        src1 = src1[:-1]
        src2 = src2[:-1]

        _, mode, size = operator.split(".")

        return currentState, f"mul.{mode}.{size} {dest} {src1} {src2}\n"

class Add(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("add.")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        operator, dest, src1, src2 = line.split()
        dest = dest[:-1]
        src1 = src1[:-1]
        src2 = src2[:-1]

        _, size = operator.split(".")

        return currentState, f"add.{size} {dest} {src1} {src2}\n"

class Sub(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("sub.")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        operator, dest, src1, src2 = line.split()
        dest = dest[:-1]
        src1 = src1[:-1]
        src2 = src2[:-1]

        _, size = operator.split(".")

        return currentState, f"sub.{size} {dest} {src1} {src2}\n"

class Shr(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("shr.")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        operator, dest, src1, src2 = line.split()
        dest = dest[:-1]
        src1 = src1[:-1]
        src2 = src2[:-1]

        _, size = operator.split(".")

        return currentState, f"shr.{size} {dest} {src1} {src2}\n"

class And(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("and.")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        operator, dest, src1, src2 = line.split()
        dest = dest[:-1]
        src1 = src1[:-1]
        src2 = src2[:-1]

        _, variant = operator.split(".")

        return currentState, f"and.{variant} {dest} {src1} {src2}\n"

class Xor(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("xor.")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        operator, dest, src1, src2 = line.split()
        dest = dest[:-1]
        src1 = src1[:-1]
        src2 = src2[:-1]

        _, variant = operator.split(".")

        return currentState, f"xor.{variant} {dest} {src1} {src2}\n"

class Not(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("not.")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        operator, dest, src1 = line.split()
        dest = dest[:-1]
        src1 = src1[:-1]

        _, variant = operator.split(".")

        return currentState, f"not.{variant} {dest} {src1}\n"

class Load(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("ld.")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        operator, dest, src = line.split()
        dest = dest[:-1]
        src = src[:-1]

        if src.startswith("["): src = src[1:]
        if src.endswith("]"): src = src[:-1]

        _, space, size = operator.split(".")

        return currentState, f"ld.{space}.{size} {dest} {src}\n"

class Store(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith("st.")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        operator, dest, src = line.split()
        dest = dest[:-1]
        src = src[:-1]

        if dest.startswith("["): dest = dest[1:]
        if dest.endswith("]"): dest = dest[:-1]

        _, space, size = operator.split(".")

        return currentState, f"st.{space}.{size} {dest} {src}\n"

class Label(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.startswith('$')

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        # Just leave as is

        return currentState, line + "\n"

class Ret(Translation):
    def match(self, line: str, currentState: TranspilerState) -> bool:
        return line.strip().startswith("ret")

    def transform(self, currentState: TranspilerState, line: str) -> tuple[TranspilerState, str]:
        return currentState, "ret\n"

def processLine(translations : list[Translation], outputFile, line : str, currentState: TranspilerState) -> tuple[TranspilerState, str]:
    line = line.strip()
    for translation in translations:
        if not translation.match(line, currentState):
            continue

        # Translation matched
        currentState, output = translation.transform(currentState, line)
        
        return currentState, output
    else:
        raise Exception("No translation matches line: " + line)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='Transpile a PTX file to a simplified format.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter # Show defaults
    )
    parser.add_argument(
        'input_file',
        help='Path to the input PTX file.'
    )
    parser.add_argument(
        'output_file',
        help='Path for the output transpiled file.'
    )
    args = parser.parse_args() # Parse command-line arguments
    ptxFile=open(args.input_file,"r")
    outputFile=open(args.output_file,"w+")

    lines=ptxFile.readlines()

    translationClasses = [
        Comment,
        Whitespace,
        Param,
        LDParam,
        Mov,
        Mad,
        Fma,
        Setp,
        Guarded,
        Branch,
        CVTA,
        Mul,
        Add,
        Sub,
        Shr,
        Load,
        Store,
        Label,
        Ret,
        And,
        Xor,
        Not
    ]

    translations = [
        c([], outputFile)
        for c in translationClasses
    ]

    for t in translations:
        t.translations = translations

    currentState = TranspilerState.PROGRAM_PARAMS

    for line in lines:
        currentState, output = processLine(translations, outputFile, line, currentState)
        outputFile.write(output)

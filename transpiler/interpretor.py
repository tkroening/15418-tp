import re

argumentList=[]
branchTargets={}
predicateCondition=0


def processLine(traceFile,line):
    if line=="\n":
        return;
    #1. first split by space
    tabSplit=line.split(" ")   
    print(tabSplit)
    
    #not a checks if not a instruction
    if len(tabSplit)==1:
        #checks if it is a parameter
        match=re.search(r'(_param_+)(\d+):(\d*)',tabSplit[0])
        if match!=None:
            #match.group(1)=_param_
            #match.group(2)=4 if ptx is _param_4
            #match.group(3)
            argumentList.append(match.group(3)) #pushed arguments onto argument List
            return

        #checks if it is a label (ex. $finishedLoop)
        match=re.search(r'\$+(\w*):\n',tabSplit[0])
        if match!=None:
            #match.group(1)=$ (ex. $ in $finishedLoop)
            #match.group(2)=the label (ex. finishedLoop)
            print(match.group(1))
            trace="unsetp "+"0 "+match.group(1)+"\n"
            traceFile.write(trace)
            return;
        #question: what should we do with teh ret command
    else:
        #match the opcode
        opCode=tabSplit[0]
        match opCode:
            case "ld.param.u64":
                match=re.search(r'ld\.param\.u64\s*\%(\w*\d*),\s*\[_param_(\d*)\]',line)
                #print(match.group(1)) #rd number
                #print(match.group(2)) #argument number
                register=match.group(1)
                argumentNumber=match.group(2)
                trace="mov "+"0 "+register+", "+argumentList[int(argumentNumber)]+"\n"
                traceFile.write(trace)
            case "mov.u32":
                match=re.search(r'mov\.u32\s*\%(\w*\d*),\s*([\%\w.]*)',line)
                # print(match.group(1)) #rd number
                # print(match.group(2)) #what we want to move in (TODO: need to implement wether if register or immediate)
                register=match.group(1)
                payload=match.group(2)
                trace="mov " +"0 "+ register+", "+payload+"\n"
                traceFile.write(trace)
            case "mad.lo.s32":  #converts to a add and a multiply trace
                regex=r'mad\.lo\.s32\s*\%(r\d*),\s*\%(r\d*),\s*\%(r\d*),\s*\%(r\d*);\n'
                match=re.search(regex,line)
                rTarget=match.group(1)
                r1=match.group(2)
                r2=match.group(3)
                r3=match.group(4)
                trace="mul " +"0 "+ rTarget+", "+r1+", "+r2+"\n"
                traceFile.write(trace)
                trace="add " +"0 "+ rTarget+", "+rTarget+", "+r3+"\n"
                traceFile.write(trace)
            case "cvt.u64.u32":
                regex=r'cvt\.u64\.u32\s*\%(rd\d*),\s*\%(r\d*);'
                match=re.search(regex,line)
                rd=match.group(1) #the double 64 bit register
                r1=match.group(2) #the single 32 bit register
                trace="mov " +"0 "+ rd+", "+r1+"\n"
            case "setp.ge.s64": #core assumption is that we expect a branch instruction as next instruction
                regex=r'setp\.ge\.s64\s*\%(p\d*),\s*\%(rd\d*),\s*\%(rd\d*);\n'
                match=re.search(regex,line)
                rd1=match.group(2)
                rd2=match.group(3)
                trace="setpGE " +"0 "+rd1+", "+rd2+", " #no newline because we need the @p1 to see label
                traceFile.write(trace)
            case "@%p1": 
                regex=r'\@\%p1\s*\w*\s*\$(\w*);\n'
                match=re.search(regex,line)
                setLabel=match.group(1)
                trace=setLabel+"\n";
                traceFile.write(trace)
            case "cvta.to.global.u64":
                print("we don't need this instruction")
            case "shl.b64":
                regex=r'shl\.b64\s*\%(\w*),\s*\%(\w*),\s*([\%\w*]);\n' #regex bug here
                match=re.search(regex,line)
                rdTarget=match.group(1)
                rdSource1=match.group(2)
                rdSource2=match.group(3)
                trace="shl " +"0 "+ rdTarget+", "+rdSource1+", "+rdSource2+"\n"
                traceFile.write(trace)
            case "add.s64":
                #add.s64         %rd9, %rd7, %rd8;
                regex=r'add\.s64\s*\%(\w*),\s*\%(\w*),\s*\%(\w*);\n' #regex bug here
                match=re.search(regex,line)
                rdTarget=match.group(1)
                rdSource1=match.group(2)
                rdSource2=match.group(3)
                trace="add " +"0 "+ rdTarget+", "+rdSource1+", "+rdSource2+"\n"
                traceFile.write(trace)
            case "ld.global.u64": #WARNING: don't linke how I implemented this
                regex=r'ld\.global\.u64\s*\%(\w*),\s*\[\%(\w*)\];\n' #regex bug here
                match=re.search(regex,line)
                rdTarget=match.group(1)
                rdSource1=match.group(2)
                trace="ld " +"0 "+ rdTarget+", "+"[]"+rdSource1+"\n"
                traceFile.write(trace)
            case "mul.lo.s64":
                regex=r'mul.lo.s64\s*\%(\w*),\s*\%(\w*),\s*\%(\w*);\n' #regex bug here
                match=re.search(regex,line)
                rdTarget=match.group(1)
                rdSource1=match.group(2)
                rdSource2=match.group(3)
                trace="mul " +"0 "+ rdTarget+", "+rdSource1+", "+rdSource2+"\n"
                traceFile.write(trace)
        









    return;

def main():
    ptxFile=open("ptx_saxpy.txt","r")
    lines=ptxFile.readlines()
    traceFile=open("trace.txt","w+")
    for line in lines:
        processLine(traceFile,line)
    return


main()
print(argumentList)
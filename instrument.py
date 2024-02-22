import sys,cmd,os
# import argparse

#Instruction type
NOT_BRANCH          = 0
INDIRECT_BRANCH     = 1
INDIRECT_CALL       = 2
RET                 = 3
DIRECT_BRANCH       = 4
DIRECT_CALL         = 5
CONDITIONAL_BRANCH  = 6
LOAD_INS            = 7
STORE_INS           = 8

#Target type
INDIRECT_TARGET = "indirect"
NEXT_BB         = "next bb"
RET_TARGET      = "ret"
NONE_TARGET     = None

#Instrument type
NOT_INSTRUMENT              = -1
CALL_ARMOR_MAIN             = 0
CALL_ARMOR_LIGHT_SAVE_X30   = 1
CALL_ARMOR_LIGHT_NO_SAVE    = 2
CALL_ARMOR_BOUNCER_SAVE_X30 = 3
CALL_ARMOR_BOUNCER_NO_SAVE  = 4
CALL_ARMOR_BOUNCER_DIRECT   = 5
CALL_ARMOR_RET_DIRECT       = 6

#Parameter
SWITCH_MIN_NUMS             = 3
FUNC_INS_MIN_NUMs           = 10
MIN_CALL_DISTANCE           = (1 << 17)

ARMOR_BOUNCER_FUNC          = "\t.align\t3\n" \
                            "\t.global\t__armor_bouncer_func1\n" \
                            "\t.type\t__armor_bouncer_func1, %function\n" \
                            "__armor_bouncer_func1:\n" \
                            ".LFB20___armor_bouncer_func1:\n" \
                            "\tret\n" \
                            "\tnop\n" \
                            ".LFE20___armor_bouncer_func1:\n" \
                            "\t.size\t__armor_bouncer_func1, .-__armor_bouncer_func1\n" \
                            "\t.align\t3\n" \
                            "\t.global\t__armor_bouncer_func2\n" \
                            "\t.type\t__armor_bouncer_func2, %function\n" \
                            "__armor_bouncer_func2:\n" \
                            ".LFB21___armor_bouncer_func2:\n" \
                            "\tret\n" \
                            "\tnop\n" \
                            ".LFE21___armor_bouncer_func2:\n" \
                            "\t.size\t__armor_bouncer_func2, .-__armor_bouncer_func2\n" \
                            "\t.align\t3\n" \
                            "\t.global\t__armor_bouncer_func3\n" \
                            "\t.type\t__armor_bouncer_func3, %function\n" \
                            "__armor_bouncer_func3:\n" \
                            ".LFB22___armor_bouncer_func3:\n" \
                            "\tret\n" \
                            "\tnop\n" \
                            ".LFE22___armor_bouncer_func3:\n" \
                            "\t.size\t__armor_bouncer_func3, .-__armor_bouncer_func3\n" \


#GNUPG
GNUPG_FUNC = ["mpih_sqr_n", "mpih_sqr_n_basecase", "mpihelp_mul", "mpihelp_mul_karatsuba_case"]

def strncmp(line, temp, n):
    if len(line) < n or len(temp) != n:
        return False
    if line[:n] == temp:
        return True
    return False

def determine_section(line):
    # if (!strncmp(line + 2, "section\t", 8) ||
    #       !strncmp(line + 2, "section ", 8) ||
    #       !strncmp(line + 2, "bss\n", 4) ||
    #       !strncmp(line + 2, "data\n", 5)) {
    #     instr_ok = 0;
    #   }
      
    #   if (!strncmp(line + 2, "text\n", 5) ||
    #       !strncmp(line + 2, "section\t.text", 13) ||
    #       !strncmp(line + 2, "section\t__TEXT,__text", 21) ||
    #       !strncmp(line + 2, "section __TEXT,__text", 21)) {
    #     instr_ok = 1;
    #   }
    if line[0] == "#" or line[1] == "#":
        temp = line[2:]
        if strncmp(temp, "section\t", 8) or strncmp(temp, "section ", 8) or strncmp(temp, "bss\n", 4) or strncmp(temp, "data\n", 5):
            res = 0
        if strncmp(temp, "text\n", 5) or strncmp(temp, "section\t.text", 13) or strncmp(temp, "section\t__TEXT,__text", 21) or strncmp(temp, "section __TEXT,__text", 21):
            res = 1
    return res

def determine_asm(line):
    if "#APP" in line:
        return 1
    elif "#NO_APP" in line:
        return 0
    
def get_branch_operation(ins):
    op = ins.split("\t")[1][:-1]
    return op

def get_condition_operation(ins):
    if "," in ins:
        op = ins.split(",")[-1][:-1].strip()
    else:
        op = ins.split("\t")[1][:-1].strip()
    return op

def get_operate_reg(ins):
    ops = ins.split(",")
    if strncmp(ins[1:], "stp", 3) or strncmp(ins[1:], "ldp", 3):
        if "x30" in ops[0] or "x30" in ops[1]:
            return "x30"
        else:
            return "other"
    elif strncmp(ins[1:], "str", 3) or strncmp(ins[1:], "ldr", 3):
        if "x30" in ops[0]:
            return "x30"
        else:
            return "other"

def analyze_ins(ins):
    if (strncmp(ins[1:], "stp", 3) or strncmp(ins[1:], "str", 3)) and "x30" in ins:
        return STORE_INS, get_operate_reg(ins)
    if (strncmp(ins[1:], "ldp", 3) or strncmp(ins[1:], "ldr", 3)) and "x30" in ins:
        return LOAD_INS, get_operate_reg(ins)
    if strncmp(ins[1:], "ret", 3):
        return RET, INDIRECT_TARGET
    if strncmp(ins[1:], "br\t", 3):
        return INDIRECT_BRANCH, INDIRECT_TARGET
    if strncmp(ins[1:], "b\t", 3):
        op = get_branch_operation(ins[1:])
        return DIRECT_BRANCH, op
    if strncmp(ins[1:], "bl\t", 3):
        op = get_branch_operation(ins[1:])
        return DIRECT_CALL, op
    if strncmp(ins[1:], "blr\t", 4):
        return INDIRECT_CALL, INDIRECT_TARGET
    if (strncmp(ins[1:], "b.", 2) or
        strncmp(ins[1:], "cbnz\t", 5) or
        strncmp(ins[1:], "cbz\t", 4) or
        strncmp(ins[1:], "tbnz\t", 5) or
        strncmp(ins[1:], "tbz\t", 4) or
        strncmp(ins[1:], "beq\t", 4) or
        strncmp(ins[1:], "bnq\t", 4) or
        strncmp(ins[1:], "bcs\t", 4) or
        strncmp(ins[1:], "bhs\t", 4) or
        strncmp(ins[1:], "bcc\t", 4) or
        strncmp(ins[1:], "blo\t", 4) or
        strncmp(ins[1:], "bmi\t", 4) or
        strncmp(ins[1:], "bpl\t", 4) or
        strncmp(ins[1:], "bvs\t", 4) or
        strncmp(ins[1:], "bvc\t", 4) or
        strncmp(ins[1:], "bhi\t", 4) or
        strncmp(ins[1:], "bls\t", 4) or
        strncmp(ins[1:], "bge\t", 4) or
        strncmp(ins[1:], "blt\t", 4) or
        strncmp(ins[1:], "bgt\t", 4) or
        strncmp(ins[1:], "ble\t", 4) or
        strncmp(ins[1:], "bal\t", 4)): 
        op = get_condition_operation(ins[1:])
        return CONDITIONAL_BRANCH, op
    return NOT_BRANCH, NONE_TARGET

def instrumenting_ins(ins):
    instrument_content = ""
    if ins.instrument_type == CALL_ARMOR_MAIN:
        instrument_content = "\tadrp\tx9, save_reg\n" \
                            "\tadd\tx9, x9, :lo12:save_reg\n" \
                            "\tstr\tx30, [x9]\n" \
                            "\tbl __armor_main\n" \
                            "\tldr\tx30, [x9]\n"
    elif ins.instrument_type == CALL_ARMOR_LIGHT_SAVE_X30:
        instrument_content = "\tadrp\tx9, save_reg\n" \
                                "\tadd\tx9, x9, :lo12:save_reg\n" \
                                "\tstr\tx30, [x9]\n" \
                                "\tbl __armor_light\n" \
                                "\tldr\tx30, [x9]\n"
    elif ins.instrument_type == CALL_ARMOR_LIGHT_NO_SAVE:
        instrument_content = "\tbl __armor_light\n"
    elif ins.instrument_type == CALL_ARMOR_BOUNCER_SAVE_X30:
        instrument_content = "\tmov\tx9, x30\n" \
                                "\tbl __armor_bouncer_func1\n" \
                                "\tmov\tx30, x9\n"
    elif ins.instrument_type == CALL_ARMOR_BOUNCER_NO_SAVE:
        instrument_content = "\tbl __armor_bouncer_func1\n"
    elif ins.instrument_type == CALL_ARMOR_BOUNCER_DIRECT:
        instrument_content = "\tbl __armor_bouncer_func1\n"
    # elif ins.instrument_type == CALL_ARMOR_RET_DIRECT:
    #     instrument_content = "\tadrp\tx9, save_reg\n" \
    #                             "\tadd\tx9, x9, :lo12:save_reg\n" \
    #                             "\tstr\tx30, [x9]\n" \
    #                             "\tbl __armor_ret\n" 
    elif ins.instrument_type == CALL_ARMOR_RET_DIRECT:
        instrument_content = "\tb __armor_light\n" 
    return instrument_content

class Instruction():
    def __init__(self, ins, line, skip_app, BBs):
        self.ins = ins
        self.line = line
        self.skip_app = skip_app
        self.save_x30 = 0
        self.overflow = 0
        self.instrument_type = NOT_INSTRUMENT
        self.BBs = BBs
    
    def analyze(self):
        self.ins_type, self.op =  analyze_ins(self.ins)
        
    def set_x30(self, save_x30):
        self.save_x30 = save_x30
        

class BasicBlock():
    def __init__(self, name, line):
        self.name = name
        self.line = line
        self.ins_list = []
        self.succ_list = []
        self.save_x30 = 0
    
    def add_ins(self, ins):
        self.ins_list.append(ins)
    
    def get_succ(self, ins):
        ins_type, ins_target = ins.ins_type, ins.op
        if ins_type == DIRECT_BRANCH or ins_type == DIRECT_CALL:
            self.succ_list.append(ins_target)
        elif ins_type == CONDITIONAL_BRANCH:
            self.succ_list.append(ins_target)
            # self.succ_list.append(NEXT_BB)
        elif ins_type == INDIRECT_BRANCH or ins_type == INDIRECT_CALL:
            self.succ_list.append(INDIRECT_TARGET)
        elif ins_type == RET:
            self.succ_list.append(RET_TARGET)
    
    
class Func():
    def __init__(self, name, line):
        self.name = name
        self.line = line
        self.BB_list = []
        self.ins_list = []
        self.succ_list = []
        
    def add_BB(self, BBs):
        self.BB_list.append(BBs)
        
    def count_ins(self):
        ins_nums = 0
        for i in range(0, len(self.BB_list)):
            ins_nums += len(self.BB_list[i].ins_list)
        return ins_nums
    
    def get_BB_index(self, BB):
        for i in range(0, len(self.BB_list)):
            if self.BB_list[i].name == BB:
                return i
        return -1
    
    def instrument_first_ins(self, instrument_type):
        first_ins = 0
        for i in range(0, len(self.BB_list)):
            for j in range(0, len(self.BB_list[i].ins_list)):
                first_ins = self.BB_list[i].ins_list[j]
                self.BB_list[i].ins_list[j].instrument_type = instrument_type
                return 0

class Analyzer():
    def __init__(self, input):
        self.input_file = input
        self.func_list = []
        self.all_BB_list = []
        self.all_ins_list = []
        self.instrument_ins = []
        self.instrument_line = []
        self.contain_main = 0
        self.call_distance = 0        
    def init_analyzer(self):
        fl = open(self.input_file, "r+")
        line_num = 0
        self.func_list = []
        skip_app = 0
        instr_ok = 0
        for line in fl.readlines():
            line_num += 1
            if len(line) < 2:
                continue
            if line[0] == "\t" and line[1] == ".":
                temp = line[2:]
                if strncmp(temp, "section\t", 8) or strncmp(temp, "section ", 8) or strncmp(temp, "bss\n", 4) or strncmp(temp, "data\n", 5):
                    instr_ok = 0
                if strncmp(temp, "text\n", 5) or strncmp(temp, "section\t.text", 13) or strncmp(temp, "section\t__TEXT,__text", 21) or strncmp(temp, "section __TEXT,__text", 21):
                    instr_ok = 1
            if line[0] == "#" or line[1] == "#":
                if "#APP" in line:
                    skip_app = 1
                if "#NO_APP" in line:
                    skip_app = 0
            if instr_ok == 0 or skip_app == 1:
                continue
            if line.startswith(".L") and len(self.func_list) > 0:
                BBs = BasicBlock(line.split(":")[0], line_num)
                self.func_list[-1].add_BB(BBs)
            elif ".type" in line.split(",")[0] and "function" in line.split(",")[1]:
                # print("Func")
                func_name = line.split(",")[0].split("\t")[-1]
                func = Func(func_name, line_num)
                self.func_list.append(func)
            elif line[0] == "\t" and line[1] != ".":
                self.call_distance += 4
                ins = Instruction(line, line_num, skip_app, self.func_list[-1].BB_list[-1])
                ins.analyze()
                self.func_list[-1].BB_list[-1].get_succ(ins)
                self.func_list[-1].BB_list[-1].add_ins(ins)
                if ins.ins_type == DIRECT_CALL:
                    self.func_list[-1].succ_list.append(ins.op)
                
        # for func in self.func_list:
        #     # if len(func.BB_list) == 0:
        #     i = 0
        #     length = len(func.BB_list)
        #     while i < length:
        #         BBs = func.BB_list[i]
        #         if len(BBs.ins_list) == 0:
        #             del func.BB_list[i]
        #             length -= 1
        #         else:
        #             i += 1
        for func in self.func_list:
            for i in range(0, len(func.BB_list)):
                BBs = func.BB_list[i]
                if len(BBs.ins_list) > 0:
                    ins = BBs.ins_list[-1]
                    BBs.get_succ(ins)
                    func.ins_list.extend(BBs.ins_list)
        
        for func in self.func_list:
            if func.name == "main":
                func.instrument_first_ins(CALL_ARMOR_MAIN)
                self.call_distance += 20
                self.contain_main = 1
            # else:
            #     if func.count_ins() > 10:
            #         func.instrument_first_ins(CALL_ARMOR_BOUNCER_SAVE_X30)
            #         self.call_distance += 12
            if func.name in func.succ_list:
                print("is recursive function")
                continue
            for i in range(0, len(func.ins_list)):
                ins = func.ins_list[i]
                if ins.ins_type == STORE_INS and ins.op == "x30":
                    ins.set_x30(1)
                elif ins.ins_type == LOAD_INS and ins.op == "x30":
                    ins.set_x30(2)
                    BBs.save_x30 = 2
                
                if ins.ins_type == CONDITIONAL_BRANCH and ins.overflow == 0:
                    switch_length = 0
                    ins_length = 0
                    curr_BB_idx = func.BB_list.index(ins.BBs)
                    overflow_ins = []
                    for k in range(i+1, len(func.ins_list)):
                        next_ins = func.ins_list[k]
                        ins_length += 1
                        overflow_ins.append(next_ins)
                        if ins_length > 3:
                            break
                        if next_ins.ins_type == CONDITIONAL_BRANCH and ins.overflow == 0:
                            target_BB_idx = func.get_BB_index(next_ins.op)
                            next_BB_idx = func.BB_list.index(next_ins.BBs)
                            if target_BB_idx <= next_BB_idx:
                                break
                            else:
                                ins_length = 0
                                switch_length += 1
                    if switch_length >= SWITCH_MIN_NUMS and ins.overflow == 0:
                        ins.instrument_type = CALL_ARMOR_LIGHT_SAVE_X30
                        for of_ins in overflow_ins:
                            of_ins.overflow = 1
                        
                # if ins.ins_type == INDIRECT_BRANCH:
                #     if ins.overflow == 0 and ins.skip_app == 0:
                #         if ins.save_x30 == 1:
                #             ins.instrument_type =  CALL_ARMOR_LIGHT_NO_SAVE
                #             self.call_distance += 4
                #         else:
                #             ins.instrument_type =  CALL_ARMOR_LIGHT_SAVE_X30
                #             self.call_distance += 20
                # elif ins.ins_type == INDIRECT_CALL:
                #     if ins.overflow == 0 and ins.skip_app == 0:
                #         ins.instrument_type =  CALL_ARMOR_LIGHT_NO_SAVE
                #         self.call_distance += 4
                # elif ins.ins_type == RET:
                #     if ins.overflow == 0 and ins.skip_app == 0:
                #         ins.instrument_type =  CALL_ARMOR_RET_DIRECT
                #         self.call_distance += 0
                # if ins.ins_type == DIRECT_CALL:
                #     if ins.overflow == 0 and ins.skip_app == 0:
                #         ins.instrument_type =  CALL_ARMOR_LIGHT_NO_SAVE
                #         self.call_distance += 16
                # if ins.ins_type == DIRECT_CALL:
                #     if ins.op in GNUPG_FUNC:
                #         ins.instrument_type =  CALL_ARMOR_LIGHT_NO_SAVE
                #         self.call_distance += 16
                            
        for i in range(0, len(self.func_list)):
            self.all_BB_list.extend(self.func_list[i].BB_list)
            for j in range(0, len(self.func_list[i].BB_list)):
                self.all_ins_list.extend(self.func_list[i].BB_list[j].ins_list)
                for k in range(0, len(self.func_list[i].BB_list[j].ins_list)):
                    ins = self.func_list[i].BB_list[j].ins_list[k]
                    if ins.instrument_type != NOT_INSTRUMENT:
                        self.instrument_ins.append(ins)
                        self.instrument_line.append(ins.line)          
        print("[*] Instrument point %s" % (len(self.instrument_line)))      
        fl.close()
                        
    def rewrite_binary(self, output):
        self.output_file = output
        output_fl = open(self.output_file, "w+")
        input_fl = open(self.input_file, "r+")
        line_nums = 0
        for line in input_fl.readlines():
            line_nums += 1
            # print(line)
            # output_fl.write(line)
            if strncmp(line, "\t.file", 6) and self.contain_main == 1:
                output_fl.write(line)
                script_dir = os.path.dirname(os.path.abspath(__file__))
                asm_path = os.path.join(script_dir, 'armor.s')
                asm_fl = open(asm_path, "r+")
                for l in asm_fl.readlines():
                    output_fl.write(l)
                continue
            if line_nums not in self.instrument_line:
                output_fl.write(line)
            else:
                idx = self.instrument_line.index(line_nums)
                ins = self.instrument_ins[idx]
                content = instrumenting_ins(ins)
                output_fl.write(content)
                if ins.instrument_type != CALL_ARMOR_RET_DIRECT:
                    output_fl.write(line)
        # if self.call_distance < MIN_CALL_DISTANCE and self.contain_main == 1:
        #     output_fl.write("\t.text\n")
        #     i = 0
        #     while i < (265164-6244-4):
        #         output_fl.write("\tnop\n")
        #         i += 4

        #     output_fl.write(ARMOR_BOUNCER_FUNC)
                
        output_fl.close()                
                

if __name__ == "__main__":
    input_asm = str(sys.argv[1])
    output_asm = str(sys.argv[2])
    # print("test python")
    # argp = argparse.ArgumentParser(description='')

    # argp.add_argument("asm", type=str, help="Input asm file to analyze and instrument")
    # argp.add_argument("outfile", type=str, help="Instrumented asm file")
    # args = argp.parse_args()
    rewriter = Analyzer(input_asm)
    rewriter.init_analyzer()
    rewriter.rewrite_binary(output_asm)
    

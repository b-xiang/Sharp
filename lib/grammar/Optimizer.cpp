//
// Created by bknun on 5/13/2018.
//

#include "Optimizer.h"
#include "../runtime/Opcode.h"
#include "../runtime/register.h"

void Optimizer::readjustAddresses(unsigned int stopAddr) {
    // TODO: do line_table as well
    for(unsigned int i = 0; i < func->exceptions.size(); i++) {
        ExceptionTable &et = func->exceptions.get(i);

        if(stopAddr <= et.start_pc)
            et.start_pc--;
        if(stopAddr <= et.end_pc)
            et.end_pc--;
        if(stopAddr <= et.handler_pc)
            et.handler_pc--;
    }
    for(unsigned int i = 0; i < func->finallyBlocks.size(); i++) {
        FinallyTable &ft = func->finallyBlocks.get(i);

        if(stopAddr <= ft.start_pc)
            ft.start_pc--;
        if(stopAddr <= ft.end_pc)
            ft.end_pc--;

        if(stopAddr >= ft.try_start_pc && stopAddr <= ft.try_end_pc)
            ft.try_end_pc--;
        if(stopAddr <= ft.try_start_pc) {
            ft.try_start_pc--;
            ft.try_end_pc--;
        }
    }

    for(unsigned int i = 0; i < func->line_table.size(); i++) {
        KeyPair<int64_t, long> &lt = func->line_table.get(i);

        if(stopAddr < lt.key && lt.key > 0)
            lt.key--;
    }

    int64_t x64, op, addr;
    for(unsigned int i = 0; i < stopAddr; i++) {
        if(i >= assembler->__asm64.size())
            break;

        x64 = assembler->__asm64.get(i);

        switch (op=GET_OP(x64)) {
            case op_SKPE:
            case op_SKNE:
            case op_GOTO:
            case op_JNE:
                addr=GET_Da(x64);

                /*
                 * We only want to update data which is referencing data below us
                 */
                if(addr >= stopAddr)
                {
                    // update address
                    assembler->__asm64.replace(i, SET_Di(x64, op, addr-1));
                }
                break;
            case op_MOVI:
                if(unique_addr_lst.find(i)) {
                    addr=GET_Da(x64);
                    //unique_addr_lst.replace(unique_addr_lst.indexof(i), i-1);

                    /*
                     * We only want to update data which is referencing data below us
                     */
                    if(addr <= stopAddr)
                        assembler->__asm64.replace(i, SET_Di(x64, op_MOVI, addr));
                    else {
                        assembler->__asm64.replace(i, SET_Di(x64, op_MOVI, addr-1));
                    }

                }
                i++;
                break;
        }
    }

    for(long long i = stopAddr; i < assembler->__asm64.size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (op=GET_OP(x64)) {
            case op_SKPE:
            case op_SKNE:
            case op_GOTO:
            case op_JNE:
                addr=GET_Da(x64);

               // cout << " address " << addr << " stop addr " << stopAddr << " in " << func->getFullName() << endl;
                /*
                 * We only want to update data which is referencing data below us
                 */
                if(addr >= stopAddr)
                {
                    // update address
                    assembler->__asm64.replace(i, SET_Di(x64, op, --addr));
                }
                break;
            case op_MOVI:
                if(unique_addr_lst.find(i+1)) {
                    addr=GET_Da(x64);
                    unique_addr_lst.replace(unique_addr_lst.indexof(i+1), i);

                    /*
                     * We only want to update data which is referencing data below us
                     */
                    if(addr <= stopAddr) {
                        assembler->__asm64.replace(i, SET_Di(x64, op_MOVI, addr));
                    } else
                        assembler->__asm64.replace(i, SET_Di(x64, op_MOVI, addr-1));
                }
                i++;
                break;
        }
    }
}

/**
 * [0x2] 2:	movl 2
 * [0x4] 4:	popobj
 *
 * to -> [0x4] 4: popl 2
 */
void Optimizer::optimizeLocalPops() {
    int64_t x64, local;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_MOVL:
                i++;

                if(GET_OP(assembler->__asm64.get(i)) == op_POPOBJ) {
                    local = GET_Da(x64);

                    assembler->__asm64.remove(i);
                    readjustAddresses(i+1);
                    assembler->__asm64.replace(--i, SET_Di(x64, op_POPL, local));

                    optimizedOpcodes++;
                    goto readjust;
                }
                break;
        }
    }
}

void Optimizer::optimize(Method *method) {
    this->assembler = &method->code;
    this->unique_addr_lst.addAll(method->unique_address_table);
    this->func=method;
    optimizedOpcodes=0;

    if(method->code.size()==0)
        return;

    optimizeLocalPops();
    optimizeRedundantMovICall();
    optimizeRedundantSelfInitilization();
    optimizeLocalPush();
    optimizeRedundantMovICall2();
    optimizeObjectPush();
    optimizeRedundantObjectTest();
    optimizeLoadLocal();
    optimizeLoadLocal_2();
    optimizeValueLoad();
    optimizeSizeof();
    optimizeBmrHendles();
    optimizeBmrHendles2();
    optimizeRedundantIncrement();
    optimizeStackInc();
    optimizeUnusedEbxAssign();
    optimizeRedundantReturn();
    optimizeRedundantMovr();
    optimizeLoadLocal_3();

    /**
     * must be last or the entire program will be rendered unstable
     * and will most likely fatally crash with (SEGV) signal
     */
    optimizeJumpBranches();
}

/**
 * [0x2] 2:	movi #10000000, ebx
 * [0x4] 4:	movr egx, ebx
 *
 * to -> [0x2] 2:	movi #10000000, egx
 */
void Optimizer::optimizeRedundantMovICall() {
    int64_t x64, reg1, reg2, reg3;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_MOVI:
                reg1 = assembler->__asm64.get(++i);

                if(GET_OP(assembler->__asm64.get(++i)) == op_MOVR) {
                    reg2 = GET_Ca(assembler->__asm64.get(i));
                    reg3 = GET_Cb(assembler->__asm64.get(i));

                    if(reg3 == reg1) {
                        assembler->__asm64.remove(i);
                        readjustAddresses(i);
                        assembler->__asm64.replace(--i, reg2); // remember registers are left outside the instruction
                        optimizedOpcodes++;

                        goto readjust;
                    }

                }
                break;
        }
    }
}

/**
 *
 * [0x7] 7:	movr ebx, cmt
 * [0x8] 8:	movr cmt, ebx
 *
 * to -> (removed)
 */
void Optimizer::optimizeRedundantSelfInitilization() {
    int64_t x64, reg1, reg2, reg3, reg4;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_MOVR:
                reg1 = GET_Ca(x64);
                reg2 = GET_Cb(x64);

                if(GET_OP(assembler->__asm64.get(++i)) == op_MOVR) {
                    reg3 = GET_Ca(assembler->__asm64.get(i));
                    reg4 = GET_Cb(assembler->__asm64.get(i));

                    if(reg2 == reg3 && reg1 == reg4) {
                        assembler->__asm64.remove(i);
                        readjustAddresses(i);

                        i--;
                        assembler->__asm64.remove(i);
                        readjustAddresses(i);

                        optimizedOpcodes+=2;
                        goto readjust;
                    }

                }
                break;
        }
    }
}

/**
 *
 * [0x0] 0:	loadl ebx, fp+0
 * [0x1] 1:	rstore ebx
 *
 * to -> [0x1] 1: ipushl #0
 */
void Optimizer::optimizeLocalPush() {
    int64_t x64, reg1, reg2, idx;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_LOADL:
                reg1 = GET_Ca(x64);
                idx = GET_Cb(x64);

                if(GET_OP(assembler->__asm64.get(++i)) == op_RSTORE) {
                    reg2 = GET_Da(assembler->__asm64.get(i));

                    if(reg1 == reg2) {
                        assembler->__asm64.remove(i);
                        readjustAddresses(i);
                        assembler->__asm64.replace(--i, SET_Di(x64, op_IPUSHL, idx)); // remember registers are left outside the instruction
                        optimizedOpcodes++;

                        goto readjust;
                    }
                }
                break;
        }
    }
}

/**
 *
 * [0x5] 5:	movi #2, ebx
 * [0x7] 7:	rstore ebx
 *
 * to -> [0x5] 5: istore #2
 */
void Optimizer::optimizeRedundantMovICall2() {
    int64_t x64, reg1, reg2, val;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_MOVI:
                val = GET_Da(x64);
                reg1 = assembler->__asm64.get(++i);

                if(GET_OP(assembler->__asm64.get(++i)) == op_RSTORE) {
                    reg2 = GET_Da(assembler->__asm64.get(i));

                    if(reg1 == reg2) {
                        assembler->__asm64.remove(i); // remove rstore
                        readjustAddresses(i);
                        i--;

                        assembler->__asm64.remove(i); // remove unused register
                        readjustAddresses(i);

                        assembler->__asm64.replace(--i, SET_Di(x64, op_ISTORE, val)); // remember registers are left outside the instruction
                        optimizedOpcodes+=2;

                        goto readjust;
                    }

                }
                break;
        }
    }
}

/**
 *
 * [0x2] 2:	movl 1
 * [0x3] 3:	pushobj
 *
 * to -> [0x3] 3: pushl 1
 */
void Optimizer::optimizeObjectPush() {
    int64_t x64, idx;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_MOVL:
                idx = GET_Da(x64);

                if(GET_OP(assembler->__asm64.get(++i)) == op_PUSHOBJ) {
                    assembler->__asm64.remove(i);
                    readjustAddresses(i);

                    assembler->__asm64.replace(--i, SET_Di(x64, op_PUSHL, idx)); // remember registers are left outside the instruction
                    optimizedOpcodes++;

                    goto readjust;
                }
                break;
        }
    }
}


/**
 * [0x55] 85: itest ebx
 * [0x56] 86: movr cmt, ebx
 *
 * to -> [0x3] 3: itest cmt
*/
void Optimizer::optimizeRedundantObjectTest() {
    int64_t x64, reg1, reg2, reg3;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_ITEST:
                reg1 = GET_Da(x64);

                if(GET_OP(assembler->__asm64.get(++i)) == op_MOVR) {
                    reg2 = GET_Cb(assembler->__asm64.get(i));
                    reg3 = GET_Ca(assembler->__asm64.get(i));

                    if(reg1 == reg2) {
                        assembler->__asm64.remove(i);
                        readjustAddresses(i);

                        assembler->__asm64.replace(--i, SET_Di(x64, op_ITEST,
                                                               reg3)); // remember registers are left outside the instruction
                        optimizedOpcodes++;

                        goto readjust;
                    }
                }
                break;
        }
    }
}

/**
 * [0x13] 19:	loadl ebx, fp+1
 * [0x14] 20:	movr egx, ebx
 *
 * to -> [0x3] 3: loadl egx, fp+1
*/
void Optimizer::optimizeLoadLocal() {
    int64_t x64, reg1, reg2, reg3, local;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_LOADL:
                reg1 = GET_Ca(x64);
                local = GET_Cb(x64);

                if(GET_OP(assembler->__asm64.get(++i)) == op_MOVR) {
                    reg2 = GET_Cb(assembler->__asm64.get(i));
                    reg3 = GET_Ca(assembler->__asm64.get(i));

                    if(reg1 == reg2) {
                        assembler->__asm64.remove(i); // remove movr
                        readjustAddresses(i);
                        i--;

                        assembler->__asm64.replace(i, SET_Ci(x64, op_LOADL, reg3, 0, local));
                        optimizedOpcodes++;

                        goto readjust;
                    }

                }
                break;
        }
    }
}

/**
 * [0x36] 54:	iaload_2 ebx, adx
 * [0x37] 55:	movr egx, ebx
 *
 * to -> [0x3] 3: iaload_2 egx, adx
*/
void Optimizer::optimizeLoadLocal_2() { // TODO: check to see of the register is affected for *+/- binary operations
    int64_t x64, reg1, reg2, reg3, reg4;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_IALOAD_2:
                reg1 = GET_Ca(x64);
                reg2 = GET_Cb(x64);

                if(GET_OP(assembler->__asm64.get(++i)) == op_MOVR) {
                    reg3 = GET_Ca(assembler->__asm64.get(i));
                    reg4 = GET_Cb(assembler->__asm64.get(i));

                    if(reg1 == reg4) {
                        assembler->__asm64.remove(i); // remove movr
                        readjustAddresses(i);
                        i--;

                        assembler->__asm64.replace(i, SET_Ci(x64, op_IALOAD_2, reg3, 0, reg2));
                        optimizedOpcodes++;

                        goto readjust;
                    }

                }
                break;
        }
    }
}
/**
 * [0x33] 51:	movi #0, adx
 * [0x35] 53:	chklen adx
 * [0x36] 54:	iaload_2 egx, adx
 *
 * to -> [0x33] 51:	movi #0, adx
 *       [0x36] 54:	iaload_2 egx, adx
*/
void Optimizer::optimizeValueLoad() {
    int64_t x64, val, reg1, reg3, reg4;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_MOVI:
                val = GET_Da(x64);
                reg1 = assembler->__asm64.get(++i);

                if(reg1 == adx && val == 0 && GET_OP(assembler->__asm64.get(++i)) == op_CHECKLEN
                   && GET_OP(assembler->__asm64.get(i+1)) == op_IALOAD_2) {
                    assembler->__asm64.remove(i); // remove movr
                    readjustAddresses(i);
                    optimizedOpcodes++;
                    goto readjust;

                }
                break;
        }
    }
}

/**
 * [0x72] 114:	sizeof ebx
 * [0x73] 115:	movr egx, ebx
 *
 * to -> [0x3] 3: sizeof egx
*/
void Optimizer::optimizeSizeof() {
    int64_t x64, reg1, reg2, reg3;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_SIZEOF:
                reg1 = GET_Da(x64);

                if(GET_OP(assembler->__asm64.get(++i)) == op_MOVR) {
                    reg2 = GET_Ca(assembler->__asm64.get(i));
                    reg3 = GET_Cb(assembler->__asm64.get(i));

                    if(reg1 == reg3) {
                        assembler->__asm64.remove(i); // remove movr
                        readjustAddresses(i);
                        i--;

                        assembler->__asm64.replace(i, SET_Di(x64, op_SIZEOF, reg2));
                        optimizedOpcodes++;

                        goto readjust;
                    }

                }
                break;
        }
    }
}

/**
 * [0x3] 3:	movbi #4, #841431442464721
 * [0x5] 5:	movr ebx, bmr
 * [0x6] 6:	nop
 * [0x7] 7:	rstore ebx
 *
 * to -> [0x3] 3:	movbi #4, #841431442464721
 *       [0x7] 7:	rstore bmr
*/
void Optimizer::optimizeBmrHendles() {
    int64_t x64, reg1, reg2;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_MOVBI:
                i++; // we dont care about the values because they go to %bmr

                if(GET_OP(assembler->__asm64.get(++i)) == op_MOVR) {
                    reg1 = GET_Ca(assembler->__asm64.get(i));
                    reg2 = GET_Cb(assembler->__asm64.get(i));

                    if(reg2 == bmr && GET_OP(assembler->__asm64.get(i+1)) == op_NOP
                       && GET_OP(assembler->__asm64.get(i+2)) == op_RSTORE
                       && GET_Da(assembler->__asm64.get(i+2)) == reg1) {
                        assembler->__asm64.remove(i); // remove movr
                        readjustAddresses(i);
                        assembler->__asm64.remove(i); // remove nop
                        readjustAddresses(i);

                        assembler->__asm64.replace(i, SET_Di(x64, op_RSTORE, bmr));
                        optimizedOpcodes+=2;

                        goto readjust;
                    }

                }
                break;
        }
    }
}

/**
 * [0x18] 24:	movbi #0, #1
 * [0x1a] 26:	movr ebx, bmr
 * [0x1b] 27:	rstore ebx
 *
 * to -> [0x3] 3:	movbi #0, #1
 *       [0x7] 7:	rstore bmr
*/
void Optimizer::optimizeBmrHendles2() {
    int64_t x64, reg1, reg2;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_MOVBI:
                i++; // we dont care about the values because they go to %bmr

                if(GET_OP(assembler->__asm64.get(++i)) == op_MOVR) {
                    reg1 = GET_Ca(assembler->__asm64.get(i));
                    reg2 = GET_Cb(assembler->__asm64.get(i));

                    double a1 = assembler->__asm64.get(i+1);
                    double a2 = assembler->__asm64.get(i+2);
                    if(reg2 == bmr && GET_OP(assembler->__asm64.get(i+1)) == op_RSTORE
                       && GET_Da(assembler->__asm64.get(i+1)) == reg1) {
                        assembler->__asm64.remove(i); // remove movr
                        readjustAddresses(i);

                        assembler->__asm64.replace(i, SET_Di(x64, op_RSTORE, bmr));
                        optimizedOpcodes++;

                        goto readjust;
                    }

                }
                break;
        }
    }
}

/**
 * [0x107] 263:	inc ebx
 * [0x108] 264:	goto @49
 *
 * to -> [0x3] 3: goto @49
*/
void Optimizer::optimizeRedundantIncrement() {
    int64_t x64, reg1;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_INC:
                reg1 = GET_Da(x64);

                if(GET_OP(assembler->__asm64.get(i+1)) == op_GOTO) {

                    if(reg1 == ebx) {
                        assembler->__asm64.remove(i); // remove inc ebx
                        readjustAddresses(i);
                        optimizedOpcodes++;
                        goto readjust;
                    }

                }
                break;
        }
    }
}

/**
 * [0x2e] 46:	smov ebx, sp+0
 * [0x2f] 47:	inc ebx
 * [0x30] 48:	smovr ebx, sp+0
 *
 * to -> [0x3] 3: isadd #1, sp+0
*/
void Optimizer::optimizeStackInc() {
    int64_t x64, reg1;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_SMOV:
                reg1 = GET_Ca(x64);

                if(GET_OP(assembler->__asm64.get(++i)) == op_INC
                   && GET_Da(assembler->__asm64.get(i)) == reg1
                   && GET_OP(assembler->__asm64.get(i+1)) == op_SMOVR
                   && GET_Ca(assembler->__asm64.get(i+1)) == reg1) {

                    assembler->__asm64.remove(i); // remove smovr ebx, sp+0
                    readjustAddresses(i);
                    assembler->__asm64.remove(i); // inc ebx
                    readjustAddresses(i);
                    i--;

                    optimizedOpcodes+=2;
                    assembler->__asm64.replace(i, SET_Ci(x64, op_ISADD, 1, 0, 0));
                    goto readjust;

                }
                break;
        }
    }
}

/**
 * [0x26] 38:	movr ebx, cmt
 * [0x27] 39:	movi #46, adx
 * [0x29] 41:	ifne
 *
 * to -> [0x27] 39:	movi #46, adx
 *       [0x29] 41:	ifne
*/
void Optimizer::optimizeUnusedEbxAssign() {
    int64_t x64, reg1, reg2;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_MOVR:
                reg1 = GET_Ca(x64);
                reg2 = GET_Cb(x64);

                if(reg2 == cmt && GET_OP(assembler->__asm64.get(i+1)) == op_MOVI
                   && assembler->__asm64.get(i+2) == adx
                   && (GET_OP(assembler->__asm64.get(i+3)) == op_IFNE
                   || GET_OP(assembler->__asm64.get(i+3)) == op_IFE)) {

                    assembler->__asm64.remove(i); // remove movr ebx, cmt
                    readjustAddresses(i);
                    i--;

                    optimizedOpcodes++;
                    goto readjust;
                }
                break;
        }
    }
}

/**
 * [0xa] 10:	movr ebx, cmt
 * [0xb] 11:	return_val ebx
 *
 * to -> [0xb] 11:	return_val cmt
*/
void Optimizer::optimizeRedundantReturn() {
    int64_t x64, reg1, reg2;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_MOVR:
                reg1 = GET_Ca(x64);
                reg2 = GET_Cb(x64);

                if(GET_OP(assembler->__asm64.get(i+1)) == op_RETURNVAL
                   && GET_Da(assembler->__asm64.get(i+1)) == reg1) {

                    assembler->__asm64.remove(i); // remove movr ebx, cmt
                    readjustAddresses(i);

                    optimizedOpcodes++;
                    assembler->__asm64.replace(i, SET_Di(x64, op_RETURNVAL, reg2));
                    goto readjust;
                }
                break;
        }
    }
}

/**
 * [0xb] 11:	movr ebx, cmt
 * [0xc] 12:	rstore ebx
 *
 * to -> [0x3] 3: rstore cmt
*/
void Optimizer::optimizeRedundantMovr() {
    int64_t x64, reg1, reg2;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_MOVR:
                reg1 = GET_Ca(x64);
                reg2 = GET_Cb(x64);

                if(GET_OP(assembler->__asm64.get(i+1)) == op_RSTORE
                   && GET_Da(assembler->__asm64.get(i+1)) == reg1) {

                    assembler->__asm64.remove(i); // remove movr ebx, cmt
                    readjustAddresses(i);

                    optimizedOpcodes++;
                    assembler->__asm64.replace(i, SET_Di(x64, op_RSTORE, reg2));
                    goto readjust;
                }
                break;
        }
    }
}

/**
 * [0x9] 9:	loadl ebx, fp+1
 * [0xa] 10:	iaddl 1, @1
 * [0xb] 11:	goto @2
 *
 * to -> [0xa] 10:	iaddl 1, @1
 *       [0xb] 11:	goto @2
*/
void Optimizer::optimizeLoadLocal_3() {
    int64_t x64, reg1;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_LOADL:
                reg1 = GET_Ca(x64);

                if(reg1 == ebx && GET_OP(assembler->__asm64.get(i+1)) == op_IADDL
                   && GET_OP(assembler->__asm64.get(i+2)) == op_GOTO) {

                    assembler->__asm64.remove(i); // remove loadl
                    readjustAddresses(i);

                    optimizedOpcodes++;
                    goto readjust;
                }
                break;
        }
    }
}

/**
 * [0x14] 20:	movi #31, adx
 * [0x16] 22:	ifne
 *
 * to -> [0xa] 10:	jne #31
*/
void Optimizer::optimizeJumpBranches() {
    int64_t x64, val, reg1;
    readjust:
    for(unsigned int i = 0; i < assembler->size(); i++) {
        x64 = assembler->__asm64.get(i);

        switch (GET_OP(x64)) {
            case op_MOVI:
                val = GET_Da(x64);
                reg1 = assembler->__asm64.get(++i);

                if(reg1 == adx && GET_OP(assembler->__asm64.get(i+1)) == op_IFNE) {

                    assembler->__asm64.remove(i); // remove movi
                    readjustAddresses(i);
                    i--;

                    assembler->__asm64.remove(i); // remove register
                    readjustAddresses(i);

                    optimizedOpcodes+=2;

                    if(val >= i) // we have to re-allign the addresses ourselves
                        val-=2;
                    assembler->__asm64.replace(i, SET_Di(x64, op_JNE, val));
                    goto readjust;
                }
                break;
        }
    }
}

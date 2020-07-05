//===========================================================================
// pin_prime.cpp utilizes PIN to feed instructions into the core model 
// at runtime
//===========================================================================
/*
Copyright (c) 2015 Princeton University
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Princeton University nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY PRINCETON UNIVERSITY "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL PRINCETON UNIVERSITY BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "portability.H"
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <syscall.h>
#include <utmpx.h>
#include <dlfcn.h>
#include "pin.H"
#include "instlib.H"
#include "core_manager.h"

using namespace std;

CoreManager *core_manager = NULL;

KNOB<int> KnobMaxMsgSize(KNOB_MODE_WRITEONCE, "pintool",
    "l", "1024", "specify max_msg_size");

KNOB<int> KnobPID(KNOB_MODE_WRITEONCE, "pintool",
    "p", "1", "specify pid of current process");

// Handle non-memory instructions
inline void execNonMem(uint32_t ins_count, THREADID threadid)
{
    core_manager->execNonMem(ins_count, threadid);
}

// Handle a memory instruction
inline void execMem(void * addr, THREADID threadid, uint32_t size, bool mem_type)
{
    core_manager->execMem(addr, threadid, size, mem_type);
}

// void syscallEntry(THREADID threadid)
// {
//     core_manager->syscallEntry(threadid);
// } 

// This routine is executed every time a thread starts.
inline void ThreadStart(THREADID threadid, CONTEXT *ctxt, int32_t flags, void *v)
{
    core_manager->threadStart(threadid, ctxt, flags, v);
}

// This routine is executed every time a thread is destroyed.
inline void ThreadFini(THREADID threadid, const CONTEXT *ctxt, int32_t code, void *v)
{
    core_manager->threadFini(threadid, ctxt, code, v);
}

// Enter a syscall
inline void SyscallEntry(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v)
{
    core_manager->syscallEntry(threadIndex, ctxt, std, v);
}

// Exit a syscall
inline void SyscallExit(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v)
{
    core_manager->syscallExit(threadIndex, ctxt, std, v);
}


// Pin calls this function every time a new basic block is encountered
void Trace(TRACE trace, void *v)
{
    // Visit every basic block  in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        uint32_t nonmem_count = 0;
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {

            // Instruments memory accesses using a predicated call, i.e.
            // the instrumentation is called iff the instruction will actually be executed.
            //
            // The IA-64 architecture has explicitly predicated instructions. 
            // On the IA-32 and Intel(R) 64 architectures conditional moves and REP 
            // prefixed instructions appear as predicated instructions in Pin.
            if(INS_IsMemoryRead(ins) || INS_IsMemoryWrite(ins)) { 
                uint32_t memOperands = INS_MemoryOperandCount(ins);

                // Iterate over each memory operand of the instruction.
                for (uint32_t memOp = 0; memOp < memOperands; memOp++) {
                    const uint32_t size = INS_MemoryOperandSize(ins, memOp);
                    // Note that in some architectures a single memory operand can be 
                    // both read and written (for instance incl (%eax) on IA-32)
                    // In that case we instrument it once for read and once for write.
                    if (INS_MemoryOperandIsRead(ins, memOp)) {
                        INS_InsertPredicatedCall(
                            ins, IPOINT_BEFORE, (AFUNPTR)execMem,
                            IARG_MEMORYOP_EA, memOp,
                            IARG_THREAD_ID,
                            IARG_UINT32, size,
                            IARG_BOOL, 0,
                            IARG_END);

                    }
                    if (INS_MemoryOperandIsWritten(ins, memOp)) {
                        INS_InsertPredicatedCall(
                            ins, IPOINT_BEFORE, (AFUNPTR)execMem,
                            IARG_MEMORYOP_EA, memOp,
                            IARG_THREAD_ID,
                            IARG_UINT32, size,
                            IARG_BOOL, 1,
                            IARG_END);
                    }
                }
            }
            else {
                nonmem_count++;
                // INS_InsertPredicatedCall(
                //     ins, IPOINT_BEFORE, (AFUNPTR)execNonMem, 
                //     IARG_UINT32, 1, 
                //     IARG_THREAD_ID, 
                //     IARG_END
                // );
            }

            // if (INS_IsSyscall(ins)) {
            //     INS_InsertPredicatedCall(
            //         ins, IPOINT_BEFORE, (AFUNPTR)syscallEntry,
            //         IARG_THREAD_ID, IARG_END);
            // }
        }
        // Insert a call to execNonMem before every bbl, passing the number of nonmem instructions
        BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)execNonMem, IARG_FAST_ANALYSIS_CALL, 
                      IARG_UINT32, nonmem_count, 
                      IARG_THREAD_ID, 
                      IARG_END);

    } // End Ins For
}



inline void Start(void *v)
{
    core_manager = new CoreManager(KnobPID.Value(), KnobMaxMsgSize.Value());
    core_manager->startSim();

}

inline void Fini(int32_t code, void *v)
{
    core_manager->finishSim(code, v);
    delete core_manager;
}


/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
   
int32_t Usage()
{
    PIN_ERROR( "This Pintool simulates a many-core cache system\n" 
              + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) return Usage();
    PIN_InitSymbols();
    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddSyscallEntryFunction(SyscallEntry, 0);
    PIN_AddSyscallExitFunction(SyscallExit, 0);
    PIN_AddThreadStartFunction(ThreadStart, 0); 
    PIN_AddThreadFiniFunction(ThreadFini, 0);   
    PIN_AddApplicationStartFunction(Start, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();

    return 0;
}

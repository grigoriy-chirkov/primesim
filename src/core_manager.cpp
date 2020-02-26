//===========================================================================
// core_manager.cpp manages the cores
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


#include <iostream>
#include <limits>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <inttypes.h>
#include <cmath>
#include <assert.h>

#include "common.h"
#include "core_manager.h"

using namespace std;


void CoreManager::init(XmlSim* xml_sim, MPI_Comm _comm)
{
    int rc;
    syscall_count = 0;

    max_msg_size = xml_sim->max_msg_size;
    comm = _comm;

    rc = MPI_Comm_rank(comm,&pid);
    if (rc != MPI_SUCCESS) {
        cerr << "Process not in the communicator. Terminating.\n";
        MPI_Abort(MPI_COMM_WORLD, rc);
    }

    for(int i = 0; i < CORE_THREAD_MAX; i++) {
        thread_data[i].init(max_msg_size);
    }
}

void CoreManager::startSim()
{
    int rc;
    auto& tdata = thread_data[0];
    auto& msg = tdata.msgs[0];
    msg.is_control = true;
    msg.message_type = PROCESS_STARTING;
    msg.tid = 0;
    msg.pid = pid;
    msg.payload_len = 1;
    MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, 0, comm);

    // Create new new communicator without uncore process to 
    // barrier all core processes before simulation start
    int barrier_rank;
    MPI_Comm   barrier_comm;
    createCommWithoutUncore(comm, &barrier_comm);

    rc = MPI_Comm_rank(barrier_comm, &barrier_rank);
    if (rc != MPI_SUCCESS) {
        cerr << "Process not in the barrier comm. Terminating.\n";
        MPI_Abort(MPI_COMM_WORLD, rc);
    }

    MPI_Barrier(barrier_comm);
}


// Handle non-memory instructions
void CoreManager::execNonMem(uint32_t ins_count_in, THREADID tid)
{
    thread_data[tid].ins_nonmem += ins_count_in;
}


// Handle a memory instruction
void CoreManager::execMem(void * addr, THREADID tid, uint32_t size, BOOL mem_type)
{
    ThreadData& tdata = thread_data[tid];
    MPIMsg& msg = tdata.msgs[tdata.mpi_pos];
    msg.is_control = false;
    msg.mem_type = mem_type;
    msg.addr_dmem = (uint64_t) addr;
    msg.ins_before = tdata.ins_nonmem;
    tdata.ins_nonmem = 0;
    
    tdata.mpi_pos++;
    if (tdata.mpi_pos >= (max_msg_size + 1)) {
        drainMemReqs(tid);
    }
}

void CoreManager::drainMemReqs(THREADID tid) {
    ThreadData& tdata = thread_data[tid];
    
    MPIMsg& msg = tdata.msgs[0];
    msg.is_control = true;
    msg.message_type = MEM_REQUESTS;
    msg.tid = tid;
    msg.pid = pid;
    msg.payload_len = tdata.mpi_pos;
    MPI_Send(tdata.msgs, tdata.mpi_pos * sizeof(MPIMsg), MPI_CHAR, 0, 0, comm);

    tdata.mpi_pos = 1;
}

// This routine is executed every time a thread starts.
void CoreManager::threadStart(THREADID tid, CONTEXT *ctxt, int32_t flags, void *v)
{
    if( tid >= CORE_THREAD_MAX ) {
        cerr << "Error: the number of threads exceeds the limit!\n";
    }
    ThreadData& tdata = thread_data[tid];
    tdata.init(max_msg_size);
    
    MPIMsg& msg = tdata.msgs[0];
    msg.is_control = true;
    msg.message_type = NEW_THREAD;
    msg.tid = tid;
    msg.pid = pid;
    msg.payload_len = 1;

    MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, 0, comm);
}


// This routine is executed every time a thread is destroyed.
void CoreManager::threadFini(THREADID tid, const CONTEXT *ctxt, int32_t code, void *v)
{
    auto& tdata = thread_data[tid];
    //Send out all remaining memory requests in the buffer
    if(tdata.mpi_pos > 1) {
        drainMemReqs(tid);
    }

    tdata.thread_state = FINISH;
    MPIMsg& msg = tdata.msgs[0];
    msg.is_control = true;
    msg.message_type = THREAD_FINISHING;
    msg.tid = tid;
    msg.pid = pid;
    msg.payload_len = 1;

    MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, 0, comm);
}


// Called before syscalls
void CoreManager::sysBefore(ADDRINT ip, ADDRINT num, ADDRINT arg0, ADDRINT arg1, ADDRINT arg2, 
               ADDRINT arg3, ADDRINT arg4, ADDRINT arg5, THREADID tid)
{
    if((num == SYS_futex &&  ((arg1&FUTEX_CMD_MASK) == FUTEX_WAIT || (arg1&FUTEX_CMD_MASK) == FUTEX_LOCK_PI))
    ||  num == SYS_wait4
    ||  num == SYS_waitid) {
        auto& tdata = thread_data[tid];
        if(tdata.thread_state == ACTIVE) {
            if(tdata.mpi_pos > 1) {
                drainMemReqs(tid);
            }
            
            tdata.thread_state = LOCKED;
            MPIMsg& msg = tdata.msgs[0];
            msg.is_control = true;
            msg.message_type = THREAD_LOCK;
            msg.tid = tid;
            msg.pid = pid;
            msg.payload_len = 1;
            MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, 0, comm);
        }


    }
    syscall_count++;
}

// Called after syscalls
void CoreManager::sysAfter(ADDRINT ret, THREADID tid)
{
    auto& tdata = thread_data[tid];
    if (tdata.thread_state == LOCKED) {
        tdata.thread_state = ACTIVE;
        MPIMsg& msg = tdata.msgs[0];
        msg.is_control = true;
        msg.message_type = THREAD_UNLOCK;
        msg.tid = tid;
        msg.pid = pid;
        msg.payload_len = 1;
        MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, 0, comm);
    }
    else {
        //tdata.cycle += syscall_cost;
    }
}



// Enter a syscall
void CoreManager::syscallEntry(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v)
{
    sysBefore(PIN_GetContextReg(ctxt, REG_INST_PTR),
        PIN_GetSyscallNumber(ctxt, std),
        PIN_GetSyscallArgument(ctxt, std, 0),
        PIN_GetSyscallArgument(ctxt, std, 1),
        PIN_GetSyscallArgument(ctxt, std, 2),
        PIN_GetSyscallArgument(ctxt, std, 3),
        PIN_GetSyscallArgument(ctxt, std, 4),
        PIN_GetSyscallArgument(ctxt, std, 5),
        threadIndex);
}

// Exit a syscall
void CoreManager::syscallExit(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v)
{
    sysAfter(PIN_GetSyscallReturn(ctxt, std), threadIndex);
}

void CoreManager::finishSim(int32_t code, void *v)
{
    auto& tdata = thread_data[0];
    auto& msg = tdata.msgs[0];
    msg.is_control = true;
    msg.message_type = PROCESS_FINISHING;
    msg.payload_len = 1;
    msg.tid = 0;
    msg.pid = pid;

    MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, 0, comm);
    MPI_Finalize();
}

CoreManager::~CoreManager()
{

}       

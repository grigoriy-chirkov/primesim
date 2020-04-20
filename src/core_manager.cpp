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

// ostream& operator<<(ostream& out, const MPIMsg& msg) {
//     out << msg.is_control << " " << msg.message_type << endl;
//     return out;
// }

void CoreManager::init(int _max_msg_size, int _num_recv_threads)
{
    int rc;
    syscall_count = 0;

    max_msg_size = _max_msg_size;
    num_recv_threads = _num_recv_threads;

    rc = MPI_Comm_dup(MPI_COMM_WORLD, &comm);
    if (rc != MPI_SUCCESS) {
        cerr << "Couldn't create new communicator. Terminating.\n";
        MPI_Abort(MPI_COMM_WORLD, rc);
    }
    rc = MPI_Comm_rank(comm, &pid);
    if (rc != MPI_SUCCESS) {
        cerr << "Process not in the communicator. Terminating.\n";
        MPI_Abort(MPI_COMM_WORLD, rc);
    }

    thread_data = new ThreadData[CORE_THREAD_MAX];
    assert(thread_data);

    // out = new ofstream("/scratch/gpfs/gchirkov/dump.txt", ios::binary);
    // assert(out != NULL);
    PIN_MutexInit(&mutex);
}

void CoreManager::startSim()
{
    // Create new new communicator without uncore process to 
    // barrier all core processes before simulation start
    MPI_Comm   barrier_comm;
    createCommWithoutUncore(comm, &barrier_comm);

    auto& tdata = thread_data[0];
    if (!tdata.valid)
        tdata.init(max_msg_size);

    auto& msg = tdata.msgs[0];
    msg.is_control = true;
    msg.message_type = PROCESS_STARTING;
    msg.tid = 0;
    msg.pid = pid;
    msg.payload_len = 1;
    lock();
    MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, tdata.recv_thread_num, comm);
    unlock();
    // dumpTrace(&msg, 1);


    int barrier_rank;
    int rc = MPI_Comm_rank(barrier_comm, &barrier_rank);
    if (rc != MPI_SUCCESS) {
        cerr << "Process not in the barrier comm. Terminating.\n";
        MPI_Abort(MPI_COMM_WORLD, rc);
    }

    MPI_Barrier(barrier_comm);
    MPI_Comm_free(&barrier_comm);
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

void CoreManager::drainMemReqs(THREADID tid) 
{
    ThreadData& tdata = thread_data[tid];
    
    MPIMsg& msg = tdata.msgs[0];
    msg.is_control = true;
    msg.message_type = MEM_REQUESTS;
    msg.tid = tid;
    msg.pid = pid;
    msg.payload_len = tdata.mpi_pos;

    lock();
    MPI_Send(tdata.msgs, (max_msg_size + 1) * sizeof(MPIMsg), MPI_CHAR, 0, tdata.recv_thread_num, comm);
    unlock();
    
    // dumpTrace(tdata.msgs, tdata.mpi_pos);

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

    lock();
    MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, num_recv_threads-1, comm);
    MPI_Recv(&tdata.recv_thread_num, 1, MPI_INT, 0, tid, comm, MPI_STATUS_IGNORE);
    unlock();
    // dumpTrace(&msg, 1);
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

    lock();
    MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, tdata.recv_thread_num, comm);
    unlock();
    // dumpTrace(&msg, 1);
}


// Called before syscalls
void CoreManager::sysBefore(ADDRINT ip, ADDRINT num, ADDRINT arg0, ADDRINT arg1, ADDRINT arg2, 
               ADDRINT arg3, ADDRINT arg4, ADDRINT arg5, THREADID tid)
{
    bool is_lock = false;
    if(num == SYS_futex) {
        ADDRINT futex_cmd = arg1&FUTEX_CMD_MASK;
        switch (futex_cmd) {
            case FUTEX_WAIT:
            case FUTEX_LOCK_PI:
            case FUTEX_FD:
            case FUTEX_WAIT_BITSET:
            case FUTEX_TRYLOCK_PI:
            case FUTEX_WAIT_REQUEUE_PI:
                is_lock = true;
            default:
                break;  // do nothing
        }
    }
    if (num == SYS_wait4) {
        is_lock = true;
    }
    if (num == SYS_waitid) {
        is_lock = true;
    }

    if (is_lock) {
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
            lock();
            MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, tdata.recv_thread_num, comm);
            unlock();
            // dumpTrace(&msg, 1);
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
        lock();
        MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, tdata.recv_thread_num, comm);
        unlock();
        // dumpTrace(&msg, 1);
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

void CoreManager::lock() 
{
    PIN_MutexLock(&mutex);
}

void CoreManager::unlock()
{
    PIN_MutexUnlock(&mutex);
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

    lock();
    MPI_Send(&msg, sizeof(MPIMsg), MPI_CHAR, 0, tdata.recv_thread_num, comm);
    unlock();
    // dumpTrace(&msg, 1);

    PIN_MutexFini(&mutex);
    delete [] thread_data;
    MPI_Comm_free(&comm);
}

void CoreManager::dumpTrace(MPIMsg* trace, size_t num) {
    // lock();
    // out->write((const char*)trace, sizeof(MPIMsg) * num);
    // unlock();
}

CoreManager::~CoreManager()
{
    // out->close();
}       

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
#include <sstream>
#include <cstring>
#include <inttypes.h>
#include <cmath>
#include <cassert>
#include <fcntl.h>
#include <sys/stat.h>

#include "common.h"
#include "core_manager.h"

using namespace std;

CoreManager::CoreManager(int _pid, int _max_msg_size) : 
    pid(_pid), max_msg_size(_max_msg_size)
{}

void CoreManager::startSim()
{
    fifo_fd = createPipe();
    MPIMsg msg;
    msg.populate_control(PROCESS_STARTING, 0, pid);
    write(fifo_fd, &msg, sizeof(MPIMsg));
}

void CoreManager::finishSim(int32_t code, void *v)
{
    MPIMsg msg;
    msg.populate_control(PROCESS_FINISHING, 0, pid);
    write(fifo_fd, &msg, sizeof(MPIMsg));
    close(fifo_fd);
}      


// This routine is executed every time a thread starts.
void CoreManager::threadStart(THREADID tid, CONTEXT *ctxt, int32_t flags, void *v)
{
    if( tid >= thread_data.size() ) {
        cerr << "Error: the number of threads exceeds the limit!\n";
    }

    MPIMsg msg;
    msg.populate_control(NEW_THREAD, tid, pid);
    write(fifo_fd, &msg, sizeof(MPIMsg));
    thread_data[tid].init(max_msg_size, createPipe(tid));
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
    MPIMsg msg;
    msg.populate_control(THREAD_FINISHING, tid, pid);
    write(tdata.fifo_fd, &msg, sizeof(MPIMsg));
    close(tdata.fifo_fd);
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
    msg.populate_mem(mem_type, uint64_t(addr), tdata.ins_nonmem);
    tdata.ins_nonmem = 0;
    tdata.mpi_pos++;
    if (tdata.mpi_pos >= max_msg_size) {
        drainMemReqs(tid);
    }
}

int CoreManager::createPipe() const
{
    auto fifo_name = string("/scratch/prime_fifo_") + to_string(pid);
    mkfifo(fifo_name.c_str(), S_IRUSR | S_IWUSR);
    auto ret = open(fifo_name.c_str(), O_WRONLY);
    assert(ret != -1);
    return ret;
}

int CoreManager::createPipe(THREADID tid) const
{
    auto fifo_name = string("/scratch/prime_fifo_") + to_string(pid) + "_" + to_string(tid);
    mkfifo(fifo_name.c_str(), S_IRUSR | S_IWUSR);
    auto ret = open(fifo_name.c_str(), O_WRONLY);
    assert(ret != -1);
    fcntl(ret, F_SETPIPE_SZ, sizeof(MPIMsg) * max_msg_size);
    return ret;
}

void CoreManager::drainMemReqs(THREADID tid) 
{
    ThreadData& tdata = thread_data[tid];
    tdata.msgs[0].populate_control(MEM_REQUESTS, tid, pid);
    write(tdata.fifo_fd, tdata.msgs.data(), tdata.mpi_pos * sizeof(MPIMsg));    
    tdata.mpi_pos = 1;
}

bool CoreManager::isLock(ADDRINT num, ADDRINT arg1) const
{
    switch (num) {
        case SYS_futex:
            ADDRINT futex_cmd = arg1&FUTEX_CMD_MASK;
            switch (futex_cmd) {
                case FUTEX_WAIT:
                case FUTEX_LOCK_PI:
                case FUTEX_FD:
                case FUTEX_WAIT_BITSET:
                case FUTEX_TRYLOCK_PI:
                case FUTEX_WAIT_REQUEUE_PI:
                    return true;
                default:
                    return false;
            }
        case SYS_wait4:
            return true;
        case SYS_waitid:
            return true;
    }
    return false;
}

// Called before syscalls
void CoreManager::sysBefore(ADDRINT num, ADDRINT arg1, THREADID tid)
{
    if (isLock(num, arg1)) { 
        auto& tdata = thread_data[tid];
        if(tdata.thread_state == ACTIVE) {
            if(tdata.mpi_pos > 1) {
                drainMemReqs(tid);
            }
            
            tdata.thread_state = LOCKED;
            MPIMsg msg;
            msg.populate_control(THREAD_LOCK, tid, pid);
            write(tdata.fifo_fd, &msg, sizeof(MPIMsg));
        }
    }
}

// Called after syscalls
void CoreManager::sysAfter(THREADID tid)
{
    auto& tdata = thread_data[tid];
    if (tdata.thread_state == LOCKED) {
        tdata.thread_state = ACTIVE;
        MPIMsg msg;
        msg.populate_control(THREAD_UNLOCK, tid, pid);
        write(tdata.fifo_fd, &msg, sizeof(MPIMsg));
    }
}

// Enter a syscall
void CoreManager::syscallEntry(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v)
{
    sysBefore(PIN_GetSyscallNumber(ctxt, std),
              PIN_GetSyscallArgument(ctxt, std, 1),
              threadIndex);
}

// Exit a syscall
void CoreManager::syscallExit(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v)
{
    sysAfter(threadIndex);
}

// void CoreManager::syscallEntry(THREADID tid)
// {
//     auto& tdata = thread_data[tid];    
//     if(tdata.thread_state == ACTIVE) {
//         if(tdata.mpi_pos > 1) {
//             drainMemReqs(tid);
//         }

//         MPIMsg msg;
//         msg.populate_control(THREAD_LOCK, tid, pid);
//         write(tdata.fifo_fd, &msg, sizeof(MPIMsg));
//     }
// }

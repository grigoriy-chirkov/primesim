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

CoreManager::CoreManager(const string& _task_id, int _pid, int _max_msg_size) : 
    task_id(_task_id), pid(_pid), max_msg_size(_max_msg_size)
{}

void CoreManager::startSim()
{
    createPipe();
    CtrlMsg msg{CtrlMsg::PROCESS_START, pid, 0};
    write(fifo_fd, &msg, sizeof(CtrlMsg));
}

void CoreManager::finishSim(int32_t code, void *v)
{
    CtrlMsg msg{CtrlMsg::PROCESS_FINISH, pid, 0};
    write(fifo_fd, &msg, sizeof(CtrlMsg));
    close(fifo_fd);
}      

// This routine is executed every time a thread starts.
void CoreManager::threadStart(THREADID tid, CONTEXT *ctxt, int32_t flags, void *v)
{
    if( tid >= thread_data.size() ) {
        cerr << "Error: the number of threads exceeds the limit!\n";
    }

    CtrlMsg msg{CtrlMsg::THREAD_START, pid, tid};
    write(fifo_fd, &msg, sizeof(CtrlMsg));
    thread_data[tid].start(task_id, pid, tid, max_msg_size);
}


// This routine is executed every time a thread is destroyed.
void CoreManager::threadFini(THREADID tid, const CONTEXT *ctxt, int32_t code, void *v)
{
    thread_data[tid].finish();
}

void CoreManager::createPipe()
{
    auto fifo_name = string("/scratch/fifo_") + task_id + "_" + to_string(pid);
    mkfifo(fifo_name.c_str(), S_IRUSR | S_IWUSR);
    fifo_fd = open(fifo_name.c_str(), O_WRONLY);
    assert(fifo_fd != -1);
}

bool isLock(ADDRINT num, ADDRINT arg1)
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

void ThreadData::start(const string& _task_id, int _pid, int _tid, int _max_msg_size)
{
    valid = true;
    task_id = _task_id; 
    pid = _pid;
    tid = _tid;
    max_msg_size = _max_msg_size; 
    msgs = std::vector<InstMsg>(max_msg_size);
    valid = true;
    state = ACTIVE;
    createPipe();
    addMsg(InstMsg::THREAD_START);
}

void ThreadData::finish()
{
    state = FINISH;
    addMsg(InstMsg::THREAD_FINISH);
    drainMsgs();
    close(fifo_fd);
}

void ThreadData::addMsg(InstMsg::Type type, uint64_t addr, bool is_unlock) 
{
    assert(pos < max_msg_size);
    if (isMem(type)) {
        msgs[pos] = {type, addr, ins_nonmem, false};        
        ins_nonmem = 0;
    } else {
        msgs[pos] = {type, 0, 0, is_unlock};
    }
    pos++;
    if (pos >= max_msg_size) {
        drainMsgs();
    }
}

void ThreadData::drainMsgs() 
{
    if (pos == 0) return;
    write(fifo_fd, msgs.data(), pos * sizeof(InstMsg));
    pos = 0;
}

void ThreadData::createPipe()
{
    auto fifo_name = string("/scratch/fifo_") + task_id + "_" + to_string(pid) + "_" + to_string(tid);
    mkfifo(fifo_name.c_str(), S_IRUSR | S_IWUSR);
    fifo_fd = open(fifo_name.c_str(), O_WRONLY);
    assert(fifo_fd != -1);
    fcntl(fifo_fd, F_SETPIPE_SZ, sizeof(InstMsg) * max_msg_size);
}
//===========================================================================
// core_manager.h 
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

#ifndef  CORE_MANAGER_H
#define  CORE_MANAGER_H

#include "portability.H"
#include <inttypes.h>
#include <array>
#include <iostream>
#include <cassert>
#include <syscall.h>
#include <utmpx.h>
#include <linux/futex.h>
#include <sys/time.h>
#include <time.h>
#include <dlfcn.h>
#include "common.h"


bool isLock(ADDRINT num, ADDRINT arg1);

struct alignas(64) ThreadData {
    enum State
    {
        DEAD    = 0,
        ACTIVE  = 1,
        LOCKED  = 2,
        FINISH  = 3
    };

    uint32_t ins_nonmem = 0;
    std::vector<InstMsg> msgs;
    int pos = 0;
    State state = DEAD;
    int fifo_fd = -1;
    bool valid = false;
    std::string task_id;
    int pid = -1;
    int tid = -1;
    int max_msg_size = 0;

    void start(const std::string& task_id, int pid, int tid, int max_msg_size);
    void finish();
    void addMsg(InstMsg::Type type, uint64_t addr = 0, bool is_unlock = false);
    
    inline void addNonMem(uint32_t ins_count_in) {
        ins_nonmem += ins_count_in;
    };

    inline void syscallBefore() {
        addMsg(InstMsg::SYSCALL_IN);
        drainMsgs();
    };

    inline void syscallEntry(ADDRINT num, ADDRINT arg1) {
        if (isLock(num, arg1) && (state = ACTIVE)) { 
            state = LOCKED;
        }
    };

    inline void syscallExit() {
        addMsg(InstMsg::SYSCALL_OUT, 0, state == LOCKED);
        state = ACTIVE;
    }

    static inline bool isMem(InstMsg::Type type) {
        return ((type == InstMsg::MEM_RD) || (type == InstMsg::MEM_WR));
    };

private:
    void drainMsgs();
    void createPipe();
};

class CoreManager
{
    public:
        CoreManager(const std::string& task_id, int pid, int max_msg_size);
        void startSim();
        void finishSim(int32_t code, void *v);
        void threadStart(THREADID tid, CONTEXT *ctxt, int32_t flags, void *v);
        void threadFini(THREADID tid, const CONTEXT *ctxt, int32_t code, void *v);
        
        inline void syscallBefore(THREADID tid) {
            thread_data[tid].syscallBefore();
        };
        
        inline void syscallEntry(THREADID tid, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v) {
            thread_data[tid].syscallEntry(PIN_GetSyscallNumber(ctxt, std),
                                          PIN_GetSyscallArgument(ctxt, std, 1));
        }
        
        inline void syscallExit(THREADID tid, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v) {
            thread_data[tid].syscallExit();
        }
        
        inline void execNonMem(uint32_t ins_count_in, THREADID tid) {
            thread_data[tid].addNonMem(ins_count_in);
        };
        inline void execMem(void * addr, THREADID tid, uint32_t size, bool mem_type){
            thread_data[tid].addMsg(mem_type ? InstMsg::MEM_WR : InstMsg::MEM_RD, uint64_t(addr));
        };

    private:
        CoreManager() = delete;
        void createPipe();

        std::array<ThreadData, MAX_THREADS_PER_PROCESS> thread_data;
        const std::string task_id;
        const int pid = -1;
        const int max_msg_size = -1;
        int fifo_fd = -1;
};




#endif // CORE_MANAGER_H 

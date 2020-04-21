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
#include <string>
#include <inttypes.h>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <iostream>
#include <cmath>
#include <string>
#include <cassert>
#include <syscall.h>
#include <utmpx.h>
#include <linux/futex.h>
#include <sys/time.h>
#include <time.h>
#include <dlfcn.h>
#include "mpi.h"
#include "common.h"


#define CORE_THREAD_MAX 1024

struct ThreadData {
    uint32_t ins_nonmem = 0;
    MPIMsg   *msgs = NULL;
    int mpi_pos = 1;
    ThreadState thread_state = DEAD;
    bool valid = false;
    int cid = -1;

    void init(int max_msg_size) {
        valid = true;
        thread_state = ACTIVE;
        msgs = new MPIMsg [max_msg_size + 1];
        assert(msgs != NULL);
        memset(msgs, 0, (max_msg_size + 1) * sizeof(MPIMsg));
    };

    ~ThreadData() {
        if (valid)
            delete[] msgs;
    };
} __attribute__ ((aligned (64)));

class CoreManager
{
    public:
        void init(int max_msg_size, int server_tid);
        void startSim();
        void finishSim(int32_t code, void *v);
        void execNonMem(uint32_t ins_count_in, THREADID threadid);
        void execMem(void * addr, THREADID threadid, uint32_t size, bool mem_type);
        void threadStart(THREADID threadid, CONTEXT *ctxt, int32_t flags, void *v);
        void threadFini(THREADID threadid, const CONTEXT *ctxt, int32_t code, void *v);
        void syscallEntry(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v);
        void syscallExit(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v);
        void dumpTrace(MPIMsg* trace, size_t num);
        ~CoreManager();        
    private:
        void drainMemReqs(THREADID threadid);
        void sysBefore(ADDRINT ip, ADDRINT num, ADDRINT arg0, ADDRINT arg1, ADDRINT arg2, 
               ADDRINT arg3, ADDRINT arg4, ADDRINT arg5, THREADID threadid);
        void sysAfter(ADDRINT ret, THREADID threadid);
        void lock();
        void unlock();
        ThreadData* thread_data;
        int syscall_count;
        int max_msg_size;
        int server_tid;
        int pid;
        MPI_Comm comm;
        PIN_MUTEX mutex;
};




#endif // CORE_MANAGER_H 

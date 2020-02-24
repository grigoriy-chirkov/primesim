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

#include <string>
#include <inttypes.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <iostream>
#include <cmath>
#include <string>
#include "portability.H"
#include <syscall.h>
#include <utmpx.h>
#include <linux/futex.h>
#include <sys/time.h>
#include <time.h>
#include <dlfcn.h>
#include "mpi.h"
#include "pin.H"
#include "instlib.H"
#include "xml_parser.h"
#include "common.h"

//For futex syscalls
typedef struct SysFutex
{
    bool valid;
    int  count;
    uint64_t addr;
    uint64_t time;
} SysFutex;


class CoreManager
{
    struct ThreadData {
        double cycle;
        double ins_nonmem;
        double ins_count;
        MPIMsg   *msgs;
        int delay;
        int mpi_pos;
        int thread_state;
        SysFutex sys_wake;
        SysFutex sys_wait;
        uint32_t  core_thread;

        void init(int max_msg_size) {
            msgs = new MPIMsg [max_msg_size + 1];
            memset(msgs, 0, (max_msg_size + 1) * sizeof(MPIMsg));
            thread_state = DEAD;
            core_thread = -1;
            mpi_pos = 1;
            memset(&sys_wake, 0, sizeof(sys_wake));
            memset(&sys_wait, 0, sizeof(sys_wait));
        };

        ~ThreadData() {
            delete[] msgs;
        };
    } __attribute__ ((aligned (64)));

    public:
        void init(XmlSim* xml_sim, MPI_Comm _comm);
        void getSimStartTime();
        void getSimFinishTime();
        void startSim();
        void finishSim(int32_t code, void *v);
        void execNonMem(uint32_t ins_count_in, THREADID threadid);
        void execMem(void * addr, THREADID threadid, uint32_t size, bool mem_type);
        void threadStart(THREADID threadid, CONTEXT *ctxt, int32_t flags, void *v);
        void threadFini(THREADID threadid, const CONTEXT *ctxt, int32_t code, void *v);
        void syscallEntry(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v);
        void syscallExit(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, void *v);
        void report(ofstream& result);
        int getRank();
        void insCount(uint32_t ins_count_in, THREADID threadid);
        ~CoreManager();        
    private:
        void drainMemReqs(THREADID threadid);
        bool isOtherThreadWaiting(THREADID threadid);
        void sysBefore(ADDRINT ip, ADDRINT num, ADDRINT arg0, ADDRINT arg1, ADDRINT arg2, 
               ADDRINT arg3, ADDRINT arg4, ADDRINT arg5, THREADID threadid);
        void sysAfter(ADDRINT ret, THREADID threadid);
        double getAvgCycle();
        void barrier(THREADID threadid);
        struct timespec sim_start_time;
        struct timespec sim_finish_time;
        ThreadData thread_data[CORE_THREAD_MAX];
        int num_threads_online;
        int max_threads;
        int barrier_counter;
        uint64_t barrier_time;
        double cpi_nonmem;
        double freq;
        int syscall_count;
        int sync_syscall_count;
        int max_msg_size;
        uint64_t thread_sync_interval;
        uint64_t proc_sync_interval;
        uint64_t syscall_cost;
        int num_recv_threads;
        PIN_LOCK thread_lock;
        PIN_MUTEX mutex;
        PIN_SEMAPHORE sem;
        int rank;
        MPI_Comm comm;

};




#endif // CORE_MANAGER_H 

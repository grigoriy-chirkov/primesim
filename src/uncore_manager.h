//===========================================================================
// uncore_manager.h 
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

#ifndef  UNCORE_MANAGER_H
#define  UNCORE_MANAGER_H

#include <string>
#include <inttypes.h>
#include <fstream>
#include <sstream>
#include <list>
#include <pthread.h> 
#include "system.h"
#include "thread_sched.h"
#include "xml_parser.h"
#include "cache.h"
#include "network.h"
#include "common.h"

#include "mpi.h"

class UncoreManager;

struct HandlerArgs {
    UncoreManager* uncore_manager;
    int tid;
};

struct HandlerData {
    pthread_t handle;
    HandlerArgs args;

    void init(UncoreManager* uncore_manager, int tid) {
        args.uncore_manager = uncore_manager;
        args.tid = tid;
    }
} __attribute__((aligned(64)));

struct CoreData {
    MPIMsg* msgs = NULL;
    uint64_t in_pos = 0;
    uint64_t out_pos = 0;
    uint64_t count = 0;
    uint64_t max_count = 0;
    pthread_mutex_t mutex; // needed to add/remove data from the buffer
    pthread_cond_t can_produce; // signaled when items are removed
    
    bool valid = false;
    int cid = -1;
    int pid = -1; 
    int tid = -1;
    bool finished = false;

    uint64_t cycle = 0;
    uint64_t segment = 0;
    uint64_t ins_nonmem = 0;
    uint64_t ins_mem = 0;
    uint64_t mem_cycles = 0;
    uint64_t nonmem_cycles = 0;

    void init(int _cid, int _pid, int _tid, uint64_t _cycle, int max_msg_size);
    ~CoreData();
    void insert_msg(const MPIMsg* inbuffer, size_t num);
    size_t eject_msg(MPIMsg* outbuffer, size_t num);
    size_t empty_count();
    void report(ofstream& report_ofstream);
} __attribute__ ((aligned (64)));

class UncoreManager
{
public:
    void init(const XmlSim* xml_sim);
    void report(const char* result_basename);
    void msgProducer(int );
    void msgConsumer(int );
    void spawn_threads();  
    void alloc_server();  
    void collect_threads();      
    ~UncoreManager();
private:
    System* sys = NULL;
    ThreadSched* thread_sched = NULL;
    HandlerData* producers = NULL;
    HandlerData* consumers = NULL;
    CoreData* core_data = NULL;

    double sim_start_time;
    double sim_finish_time;

    pthread_mutex_t mutex;
    MPI_Comm   comm;
    int proc_num = 0;
    int num_threads_live = 0;
    std::vector<int>* segment_cnt = NULL;
    uint64_t cur_segment = 0;

    int max_msg_size;
    int num_cons_threads;
    int num_prod_threads;
    uint64_t thread_sync_interval;
    uint64_t proc_sync_interval;
    double cpi_nonmem;
    double freq;
    //ifstream* in;

    void lock();
    void unlock();
    void add_proc(int pid);
    void rm_proc(int pid);
    int allocCore(int pid, int tid);
    int getCoreId(int pid, int tid);
    int getProcId(int cid);
    int getCoreCount();
    uint64_t getCycle();
    int uncore_access(int core_id, InsMem* ins_mem, int64_t timer);
    double getAvgCycle();
    void getSimStartTime();
    void getSimFinishTime();
    void reserveSegmentCntSpace(uint64_t num);
};




#endif // UNCORE_MANAGER_H 

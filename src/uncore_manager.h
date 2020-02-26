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

#define BUF_SIZE 1024

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
};

struct CoreData {
    MPIMsg* msgs = NULL;
    uint64_t in_pos = 0;
    uint64_t out_pos = 0;
    uint64_t count = 0;
    uint64_t max_count = 0;

    pthread_mutex_t mutex; // needed to add/remove data from the buffer
    pthread_cond_t can_produce; // signaled when items are removed
    bool valid = false;
    int cid;
    int pid; 
    int tid;

    uint64_t cycle = 0;
    uint64_t start_cycle = 0;
    uint64_t ins_nonmem = 0;
    uint64_t ins_mem = 0;
    uint64_t mem_cycles = 0;
    uint64_t nonmem_cycles = 0;

    void init(int _cid, int _pid, int _tid, uint64_t _cycle, int max_msg_size) {
        max_count = (max_msg_size + 1) * BUF_SIZE;
        msgs = new MPIMsg[max_count];
        assert(msgs != NULL);
        memset(msgs, 0, max_count*sizeof(MPIMsg));
        in_pos = 0;
        out_pos = 0;
        count = 0;

        valid = true;
        cid = _cid;
        pid = _pid;
        tid = _tid;
        cycle = start_cycle = _cycle;

        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&can_produce, NULL);
    };

    ~CoreData() {
        if (valid) {
            delete [] msgs;
            pthread_mutex_destroy(&mutex);
            pthread_cond_destroy(&can_produce);
        }
    }

    void insert_msg(const MPIMsg* inbuffer, size_t num) {
        pthread_mutex_lock(&mutex);
        while(count + num >= max_count) { // full
        // wait until some elements are consumed
            pthread_cond_wait(&can_produce, &mutex);
        }

        if (num > max_count - in_pos){
            uint64_t part1 = max_count - in_pos;
            uint64_t part2 = num - part1;    
            memcpy(msgs+in_pos, inbuffer, sizeof(MPIMsg)*part1);
            memcpy(msgs, inbuffer+part1, sizeof(MPIMsg)*part2);  
        } else {
            memcpy(msgs+in_pos, inbuffer, sizeof(MPIMsg)*num);
        }

        count += num;
        in_pos = (in_pos + num) % max_count;
        pthread_mutex_unlock(&mutex);
    }

    bool eject_msg(MPIMsg* outbuffer) {
        pthread_mutex_lock(&mutex);
        if (count == 0) {
            pthread_mutex_unlock(&mutex);
            return false;
        }

        memcpy(outbuffer, msgs+out_pos, sizeof(MPIMsg));
        out_pos = (out_pos + 1) % max_count;
        count--;
        pthread_cond_signal(&can_produce);
        pthread_mutex_unlock(&mutex);
        return true;
    }

    void report(ofstream& report_ofstream) {
        uint64_t ins_count = ins_mem + ins_nonmem;
        uint64_t cycle_count = nonmem_cycles + mem_cycles;
        report_ofstream << "---------------------------------------------------------\n";
        report_ofstream << "Core " <<cid<< " runs " << ins_count << " instructions in " << cycle_count <<" cycles" << endl;
        report_ofstream << "Core " <<cid<< " average IPC = "<< double(ins_count) / double(cycle_count) << endl;
        report_ofstream << "Core " <<cid<< " memory instructions: "<< ins_mem <<endl;
        report_ofstream << "Core " <<cid<< " memory access cycles: "<< mem_cycles <<endl;
        report_ofstream << "Core " <<cid<< " non-memory instructions: "<< ins_nonmem <<endl;
        report_ofstream << "Core " <<cid<< " non-memory cycles: "<< nonmem_cycles <<endl;
    }
} __attribute__ ((aligned (64)));

class UncoreManager
{
public:
    void init(const XmlSim* xml_sim);
    void report(const char* result_basename);
    void msgProducer();
    void msgConsumer(int );
    void spawn_threads();  
    void collect_threads();      
    ~UncoreManager();
private:
    System* sys;
    ThreadSched* thread_sched;
    HandlerData* handler_data;
    CoreData* core_data;
    std::list<int>* proc_list;
    pthread_t producer_thread_handle;

    struct timespec sim_start_time;
    struct timespec sim_finish_time;

    pthread_mutex_t mutex;
    MPI_Comm   comm;
    bool simulation_finished;

    uint64_t barrier_cycle;
    int barrier_cnt;
    int num_threads_live;

    int max_msg_size;
    int num_recv_threads;
    uint64_t thread_sync_interval;
    uint64_t proc_sync_interval;
    double cpi_nonmem;
    double freq;

    void lock();
    void unlock();
    void add_proc(int pid);
    void rm_proc(int pid);
    int allocCore(int pid, int tid);
    int deallocCore(int pid, int tid);
    int getCoreId(int pid, int tid);
    int getProcId(int cid);
    int getCoreCount();
    uint64_t getCycle();
    int uncore_access(int core_id, InsMem* ins_mem, int64_t timer);
    double getAvgCycle();
    void getSimStartTime();
    void getSimFinishTime();
};




#endif // UNCORE_MANAGER_H 

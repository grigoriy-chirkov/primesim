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

#define BUF_SIZE 10

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
    MPIMsg* msgs[BUF_SIZE];
    pthread_mutex_t mutex; // needed to add/remove data from the buffer
    pthread_cond_t can_produce; // signaled when items are removed
    int len;
    bool valid = false;
    uint64_t cycle;

    void init(int max_msg_size) {
        for (int i = 0; i < BUF_SIZE; i++) {
            msgs[i] = new MPIMsg[max_msg_size + 1];
            memset(msgs[i], 0, (max_msg_size + 1)*sizeof(MPIMsg));
            assert(msgs[i] != NULL);
        }
        valid = true;
    };

    ~CoreData() {
        if (valid) {
            for (int i = 0; i < BUF_SIZE; i++) {
                delete[] msgs[i];
            }
        }

    }

    void insert_msg(MPIMsg* inbuffer, size_t num) {
        lock();
        if(len == BUF_SIZE) { // full
        // wait until some elements are consumed
            pthread_cond_wait(&can_produce, &mutex);
        }
        memcpy(msgs[len], inbuffer, sizeof(MPIMsg)*num);
        len++;
        unlock();
    }

    bool eject_msg(MPIMsg* outbuffer) {
        lock();
        if (len == 0) {
            unlock();
            return false;
        }

        len--;
        memcpy(outbuffer, msgs[len], sizeof(MPIMsg)*msgs[len][0].payload_len);
        unlock();
        return true;
    }

    void lock() {
        pthread_mutex_lock(&mutex);
    }

    void unlock() {
        pthread_mutex_unlock(&mutex);
    }
};

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
    pthread_t producer_thread_handle1;

    struct timespec sim_start_time;
    struct timespec sim_finish_time;

    pthread_mutex_t mutex;
    int max_msg_size;
    int num_threads;
    MPI_Comm   comm;
    bool simulation_finished;

    void lock();
    void unlock();
    void add_proc(int pid);
    void rm_proc(int pid) ;
    int getNumThreads();
    int allocCore(int pid, int tid);
    int deallocCore(int pid, int tid);
    int getCoreId(int pid, int tid);
    int getCoreCount();
    int uncore_access(int core_id, InsMem* ins_mem, int64_t timer);
    void getSimStartTime();
    void getSimFinishTime();
};




#endif // UNCORE_MANAGER_H 

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
#include <ostream>
#include <thread> 
#include <condition_variable>
#include <atomic>
#include "system.h"
#include "xml_parser.h"
#include "cache.h"
#include "network.h"
#include "common.h"

#include "mpi.h"

struct alignas(64) CoreData {
    InstMsg* msgs = NULL;
    uint64_t in_pos = 0;
    uint64_t out_pos = 0;
    uint64_t count = 0;
    uint64_t max_count = 0;
    std::mutex local_mutex; // needed to add/remove data from the buffer
    std::condition_variable can_produce; // signaled when items are removed
    
    int cid = -1;
    int pid = -1; 
    int tid = -1;
    bool valid = false;
    bool finished = false;
    bool locked = false;

    uint64_t cycle = 0;
    uint64_t segment = 0;
    uint64_t ins_nonmem = 0;
    uint64_t ins_mem = 0;
    uint64_t mem_cycles = 0;
    uint64_t nonmem_cycles = 0;

    void init(int _cid, int _pid, int _tid, uint64_t _cycle, int max_msg_size);
    ~CoreData();
    void insert_msg(const InstMsg* inbuffer, size_t num);
    size_t eject_msg(InstMsg* outbuffer, size_t num);
    void report(std::ofstream& report_ofstream);

    inline size_t empty_count() {
        return max_count - count;
    };
}; 

class UncoreManager
{
public:
    UncoreManager(const XmlSim &xml_sim);
    void report(const char* result_basename);
    void msgProducer(int );
    void msgConsumer(int );
    void spawn_threads();  
    void alloc_server();  
    void collect_threads();      
    ~UncoreManager();

    const int num_cores;
private:
    UncoreManager() = delete;
    System sys;
    std::vector<CoreData> core_data;
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    double sim_start_time = 0.0;
    double sim_finish_time = 0.0;

    std::mutex print_mutex;
    std::mutex cnt_mutex;
    MPI_Comm   comm;

    std::vector<int> segment_cnt;
    std::atomic<int> num_proc = 0;
    std::atomic<int> num_threads_live = 0;
    std::atomic<int> num_threads = 0;
    std::atomic<int> next_empty_core = 0;
    std::atomic<uint64_t> cur_segment = 0;

    const int max_msg_size;
    const int num_cons_threads;
    const int num_prod_threads;
    const uint64_t thread_sync_interval;
    const double cpi_nonmem;
    const double freq;

    uint64_t getCycle() const;
    double getAvgCycle() const;
    void getSimStartTime();
    void getSimFinishTime();

    inline void reserveSegmentCntSpace(uint64_t num) 
    {
        if (segment_cnt.size() <= num+1) {
            segment_cnt.resize(2*num+1, 0);
        }
    }
    inline int uncore_access(int core_id, InsMem* ins_mem, int64_t timer) {
        return sys.access(core_id, ins_mem, timer);
    }

};




#endif // UNCORE_MANAGER_H 

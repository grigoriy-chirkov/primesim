//===========================================================================
// uncore_manager.cpp manages the uncore system
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
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <inttypes.h>
#include <cmath>
#include <cassert>
#include <algorithm>
#include <numeric>
#include <unistd.h>

#include "uncore_manager.h"
#include "common.h"

using namespace std;

UncoreManager::UncoreManager(const XmlSim& xml_sim) :
    num_cores(xml_sim.sys.num_cores), 
    sys(xml_sim.sys), 
    core_data(xml_sim.sys.num_cores),
    max_msg_size(xml_sim.max_msg_size),
    num_cons_threads(xml_sim.num_cons_threads),
    num_prod_threads(xml_sim.num_prod_threads),
    thread_sync_interval(xml_sim.thread_sync_interval),
    cpi_nonmem(xml_sim.sys.cpi_nonmem),
    freq(xml_sim.sys.freq) 
{
    int rc = MPI_Comm_dup(MPI_COMM_WORLD, &comm);    
    if (rc != MPI_SUCCESS) {
        cerr << "Error duplicating world communicator. Terminating." << endl;
        MPI_Abort(MPI_COMM_WORLD, rc);
    }

    // Match call to createCommWithoutUncore here with same call in core_manager, 
    // otherwise process will hang forever
    MPI_Comm   barrier_comm;
    createCommWithoutUncore(comm, &barrier_comm);
}

void UncoreManager::alloc_server()
{
    CtrlMsg msg;

    while (1) {
        MPI_Recv(&msg, sizeof(CtrlMsg), MPI_CHAR, MPI_ANY_SOURCE, server_tag, comm, MPI_STATUS_IGNORE);
        switch (msg.type) {
            case CtrlMsg::PROCESS_START: { //Receive a msg indicating a new process   
                num_proc++;             
                cout << "[PriME] Pin process " << msg.pid << " begins" << endl;
                break;
            }
            case CtrlMsg::PROCESS_FINISH: {
                num_proc--;
                cout << "[PriME] Pin process " << msg.pid << " finishes"<<endl;
                if (num_proc == 0) 
                    return;
                break;
            }
            case CtrlMsg::THREAD_START: {//Receive a msg indicating a new thread
                int cid = next_empty_core;
                next_empty_core++;   
                if (cid >= num_cores) {
                    cerr << "Not enough cores for process " << msg.pid << " thread " << msg.tid << endl;
                    MPI_Abort(MPI_COMM_WORLD, -1);
                } 
                core_data[cid].init(cid, msg.pid, msg.tid, getCycle(), max_msg_size);
                MPI_Send(&cid, 1, MPI_INT, msg.pid, msg.tid, comm);
                break;
            }
            default: {
                cerr << "Wrong msg type in allocServer from process " << msg.pid << " thread " << msg.tid << endl;
                MPI_Abort(MPI_COMM_WORLD, -1);
            }
        }
    }
}

void UncoreManager::msgProducer(int my_tid) 
{
    auto inbuffer = make_unique<InstMsg[]>(max_msg_size);
    assert(inbuffer != NULL);
    auto& msg = inbuffer[0];

    while (1) {
        bool throttle = true;
        for (int cid = my_tid; cid < num_cores; cid += num_prod_threads) {
            auto& cdata = core_data[cid];
            if (!cdata.valid || cdata.finished)
                continue;

            // Check that we have msg incoming
            int msg_here = 0;
            MPI_Status status;
            MPI_Iprobe(MPI_ANY_SOURCE, cid, comm, &msg_here, &status);
            if (!msg_here) 
                continue;

            // Check that we have place to store the message
            int msg_size = 0;
            MPI_Get_count(&status, MPI_CHAR, &msg_size);
            unsigned msg_count = msg_size / sizeof(InstMsg);
            if (cdata.empty_count() < msg_count)
                continue;
            // Finally receive message
            MPI_Recv(inbuffer.get(), msg_size, MPI_CHAR, MPI_ANY_SOURCE, cid, comm, MPI_STATUS_IGNORE);
            cdata.insert_msg(inbuffer.get(), msg_count);
            throttle = false;
        }
        if (throttle) {
            if (num_threads == 0 && cur_segment > 0) {
                return;
            } else {
                this_thread::yield();
            }
        }
    }
}

void UncoreManager::msgConsumer(int my_tid) 
{
    constexpr uint64_t batch_size = 4;
    InstMsg msg_buffer[batch_size] = {};

    while (1) {
        for (int cid = my_tid; cid < num_cores; cid += num_cons_threads) {
            auto& cdata = core_data[cid];
            if (!cdata.valid || cdata.finished)
                continue;

            if (cdata.segment > cur_segment) 
                continue;

            uint64_t msg_num = cdata.eject_msg(msg_buffer, batch_size);
            if (msg_num == 0) 
                continue;

            for (uint64_t i = 0; i < msg_num; i++) {
                auto& msg = msg_buffer[i];
                switch (msg.type) {
                    case InstMsg::THREAD_START: {
                        num_threads++;
                        num_threads_live++;
                        lock_guard<mutex> lck(print_mutex);
                        cout << "[PriME] Pin thread " << cdata.tid << " from process " << cdata.pid << " begins in core " << cid << endl;
                        break;
                    }
                    case InstMsg::THREAD_FINISH: {
                        num_threads--;
                        num_threads_live--;                        
                        cdata.finished = true;
                        lock_guard<mutex> lck(print_mutex);
                        cout << "[PriME] Thread " << cdata.tid << " from process " << cdata.pid << " finishes" << endl;
                        break;
                    }
                    case InstMsg::SYSCALL_IN:{
                        num_threads_live--;
                        cdata.locked = true;
                        break;
                    }
                    case InstMsg::SYSCALL_OUT: {
                        if (msg.is_unlock) {
                            cdata.cycle = getCycle();                        
                        }
                        num_threads_live++;
                        cdata.locked = false;
                        break;
                    }
                    case InstMsg::MEM_RD:
                    case InstMsg::MEM_WR: {
                        auto nonmem_inst = msg.ins_before; 
                        auto nonmem_cycles = ceil(msg.ins_before * cpi_nonmem);
                        cdata.nonmem_cycles += nonmem_cycles;
                        cdata.cycle += nonmem_cycles;
                        cdata.ins_nonmem += nonmem_inst;

                        InsMem ins_mem = {msg.isWr(), cdata.pid, msg.addr};
                        auto mem_cycles = uncore_access(cid, &ins_mem, cdata.cycle);
                        cdata.cycle += mem_cycles;
                        cdata.mem_cycles += mem_cycles;
                        cdata.ins_mem++;
                        break;
                    }
                    default:{
                        cerr << "Wrong message type " << msg.type << endl;
                        MPI_Abort(MPI_COMM_WORLD, -1);
                    }
                }

            }

            uint64_t new_seg = cdata.cycle / thread_sync_interval;
            if (new_seg > cdata.segment) {
                lock_guard<mutex> lck(cnt_mutex);
                reserveSegmentCntSpace(new_seg);
                for (uint64_t i = cdata.segment; i < new_seg; i++) {
                    segment_cnt[i] += 1;
                }
            }
            cdata.segment = new_seg;
        }

        cnt_mutex.lock();            
        reserveSegmentCntSpace(cur_segment);
        auto cnt_value = segment_cnt[cur_segment];
        cnt_mutex.unlock();

        if (cnt_value >= max(num_threads_live.load(), 1)) {
            cur_segment++;
            if (cur_segment % 10000 == 0) {
                lock_guard<mutex> lck(print_mutex);
                cout << "[PriME] Segment " << cur_segment << " finished" << endl;
            }
        }

        if (num_threads == 0 && cur_segment > 0) {
            return;
        }
    }
}


void UncoreManager::getSimStartTime()
{
    struct timespec buffer;
    clock_gettime(CLOCK_REALTIME, &buffer);
    sim_start_time = double(buffer.tv_sec) + double(buffer.tv_nsec) / 1000000000.0; 
}

void UncoreManager::getSimFinishTime()
{
    struct timespec buffer;
    clock_gettime(CLOCK_REALTIME, &buffer);
    sim_finish_time = double(buffer.tv_sec) + double(buffer.tv_nsec) / 1000000000.0; 
}

void UncoreManager::report(const char* result_basename)
{ 
    ofstream result_ofstream;
    result_ofstream.open(result_basename);

    uint64_t total_ins_count = 0;
    uint64_t total_nonmem_ins_count = 0;
    uint64_t total_mem_ins_count = 0;
    uint64_t total_cycles = 0;
    for (const auto& cdata : core_data) {
        if (!cdata.valid) continue;
        total_mem_ins_count += cdata.ins_mem;
        total_nonmem_ins_count += cdata.ins_nonmem;
        uint64_t local_cycles = cdata.nonmem_cycles + cdata.mem_cycles;
        if (total_cycles < local_cycles) 
            total_cycles = local_cycles;
    }
    total_ins_count = total_mem_ins_count + total_nonmem_ins_count;
    // total_cycles = getCycle();

    result_ofstream << "*********************************************************\n";
    result_ofstream << "*                   PriME Simulator                     *\n";
    result_ofstream << "*********************************************************\n\n";
    double sim_time = sim_finish_time - sim_start_time;
    result_ofstream << "Total computation time: " << sim_time <<" seconds" << endl;
    result_ofstream << "Total Execution time = " << total_cycles/(freq*pow(10.0,9)) <<" seconds\n";
    result_ofstream << "System frequency = "<< freq <<" GHz" << endl;
    result_ofstream << "Simulated slowdown : " << sim_time/(total_cycles/(freq*pow(10.0,9))) <<"X\n";
    result_ofstream << "Simulation speed: " << (total_ins_count/1000000.0)/sim_time << " MIPS" << endl; 
    result_ofstream << "Simulation runs " << total_ins_count <<" instructions, " << total_cycles <<" cycles\n";
    result_ofstream << "The average IPC = "<< double(total_ins_count) / double(total_cycles) << endl;
    result_ofstream << "Total memory instructions: "<< total_mem_ins_count <<endl;
    result_ofstream << "Total non-memory instructions: "<< total_nonmem_ins_count <<endl;
    
    //result_ofstream << "System call count : " << syscall_count <<endl;
    for(int i = 0; i < num_cores; i++) {
        if (core_data[i].valid)
            core_data[i].report(result_ofstream);
    }
    result_ofstream << endl;
    
    sys.report(result_ofstream);
    result_ofstream.close();
}

UncoreManager::~UncoreManager()
{
    MPI_Comm_free(&comm);
}     

void UncoreManager::spawn_threads() 
{
    getSimStartTime();
    for(int t = 0; t < num_cons_threads; t++) {
        cout << "[PriME] Main: creating consumer thread " << t <<endl;
        consumers.push_back(thread(&UncoreManager::msgConsumer, this, t));
    }

    for(int t = 0; t < num_prod_threads; t++) {
        cout << "[PriME] Main: creating producer thread " << t <<endl;
        producers.push_back(thread(&UncoreManager::msgProducer, this, t));
    } 
}

void UncoreManager::collect_threads() 
{
    int idx = 0;
    for(auto& t : consumers) { 
        t.join();
        cout << "[PriME] Main: completed join with consumer thread " << idx << endl;
        idx++;
    }

    idx = 0;
    for(auto& t : producers) { 
        t.join();
        cout << "[PriME] Main: completed join with producer thread " << idx << endl;
        idx++;
    }
    getSimFinishTime();
}

uint64_t UncoreManager::getCycle() const
{
#if 0
    return max_element(core_data.begin(), core_data.end(), [](const CoreData& c1, const CoreData& c2) {
        return c1.cycle < c2.cycle;
    })->cycle;
#endif

#if 0
    int num = accumulate(core_data.begin(), core_data.end(), 0, [](int acc, const CoreData& c) {
        return acc + (c.valid & !c.locked);
    });
    uint64_t cycles = accumulate(core_data.begin(), core_data.end(), 0, [](uint64_t acc, const CoreData& c) {
        return acc + (c.valid & !c.locked) * c.cycle;
    });
    if (num == 0) return 0;
    return cycles / num;
#endif
    // auto seg = cur_segment.load();
    return cur_segment * thread_sync_interval;
}

void CoreData::init(int _cid, int _pid, int _tid, uint64_t _cycle, int max_msg_size) 
{
    constexpr int BUF_SIZE = 1;
    max_count = max_msg_size*BUF_SIZE;
    msgs = new InstMsg[max_count];
    assert(msgs != NULL);
    memset(msgs, 0, max_count*sizeof(InstMsg));

    valid = true;
    cid = _cid;
    pid = _pid;
    tid = _tid;
    cycle = _cycle;
};

CoreData::~CoreData() 
{
    if (valid) {
        delete [] msgs;
    }
}


void CoreData::insert_msg(const InstMsg* inbuffer, size_t num) 
{
    unique_lock<mutex> lck(local_mutex);
    can_produce.wait(lck, [this, num]{ return count + num <= max_count; });

    if (num > max_count - in_pos){
        uint64_t part1 = max_count - in_pos;
        uint64_t part2 = num - part1;    
        memcpy(msgs+in_pos, inbuffer, sizeof(InstMsg)*part1);
        memcpy(msgs, inbuffer+part1, sizeof(InstMsg)*part2);  
    } else {
        memcpy(msgs+in_pos, inbuffer, sizeof(InstMsg)*num);
    }

    count += num;
    in_pos = (in_pos + num) % max_count;
}

size_t CoreData::eject_msg(InstMsg* outbuffer, size_t num) 
{
    unique_lock<mutex> lck(local_mutex);
    if (count == 0) {
        return 0;
    }

    size_t out_num = min(num, count);
    if (out_num > max_count - out_pos){
        uint64_t part1 = max_count - out_pos;
        uint64_t part2 = out_num - part1;    
        memcpy(outbuffer, msgs+out_pos, sizeof(InstMsg)*part1);
        memcpy(outbuffer+part1, msgs, sizeof(InstMsg)*part2);  
    } else {
        memcpy(outbuffer, msgs+out_pos, sizeof(InstMsg)*out_num);
    }
    out_pos = (out_pos + out_num) % max_count;
    count -= out_num;

    lck.unlock();
    can_produce.notify_one();
    return out_num;
}

void CoreData::report(ofstream& report_ofstream) 
{
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


//===========================================================================
// system.h 
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

#ifndef  SYSTEM_H
#define  SYSTEM_H

#include <string>
#include <inttypes.h>
#include <fstream>
#include <sstream>
#include "xml_parser.h"
#include "cache.h"
#include "network.h"
#include "page_table.h"
#include "dram.h"

enum SysType
{
    DIRECTORY = 0,
    BUS = 1
};

enum Protocol
{
    MESI = 0,
    WRITE_UPDATE = 1
};

enum ProtocolType
{
    FULL_MAP = 0,
    LIMITED_PTR = 1
};

enum MemType
{
    INV   = -1, // invalid
    RD    = 0,  //read
    WR    = 1,  //write
    WB    = 2   //writeback
};


struct alignas(64) CacheLevel
{
    const int         level = 0;
    const int         share = 0;
    const int         num_caches = 0;
    const int         access_time = 0;
    const uint64_t    size = 0;
    const uint64_t    num_ways = 0;
    const uint64_t    block_size = 0;

    CacheLevel(int _level, int _share, int _num_caches, 
               int _access_time, uint64_t _size, 
               uint64_t _block_size, uint64_t _num_ways) :
        level(_level),
        share(_share),
        num_caches(_num_caches),
        access_time(_access_time),
        size(_size),
        num_ways(_num_ways),
        block_size(_block_size)
    {};

    CacheLevel() = delete;
};




class System
{
    public:
        System(const XmlSys& xml_sys);
        Cache* init_caches(int level, int cache_id);
        void init_directories(int home_id);
        int access(int core_id, InsMem* ins_mem, int64_t timer);
        State mesi_bus(Cache* cache_cur, int level, int cache_id, int core_id, InsMem* ins_mem, int64_t timer);
        State write_update_bus(Cache* cache_cur, int level, int cache_id, int core_id, InsMem* ins_mem, int64_t timer);
        State mesi_directory(Cache* cache_cur, int level, int cache_id, int core_id, InsMem* ins_mem, int64_t timer);
        int share(Cache* cache_cur, InsMem* ins_mem);
        int share_children(Cache* cache_cur, InsMem* ins_mem);
        int inval(Cache* cache_cur, InsMem* ins_mem);
        int inval_children(Cache* cache_cur, InsMem* ins_mem);
        int modify(Cache* cache_cur, InsMem* ins_mem, bool shared = false);
        int modify_children(Cache* cache_cur, InsMem* ins_mem, bool shared = false);
        int accessDirectoryCache(int cache_id, int home_id, InsMem* ins_mem, int64_t timer, State* state);
        int accessSharedCache(int cache_id, int home_id, InsMem* ins_mem, int64_t timer, State* state);
        int allocHomeId(int num_homes, uint64_t addr);
        int getHomeId(InsMem *ins_mem);
        int tlb_translate(InsMem *ins_mem, int core_id, int64_t timer);
        inline int getCoreCount() {
            return num_cores;
        }
        void report(std::ofstream& result_ofstream);
        inline int get_parent_cache_id(int cache_id, int level) {
            return cache_id*cache_level[level].share/cache_level[level+1].share;    
        }
        ~System();        
    private:
        System() = delete;
        const int        sys_type;
        const int        protocol_type;
        const int        protocol;
        const int        max_num_sharers;
        const int        num_cores;
        const int        dram_access_time;
        const int        num_levels;
        const int        page_size;
        const int        tlb_enable;
        const int        shared_llc;
        const int        verbose_report;
        const XmlSys&    xml_sys;

        bool       *hit_flag;
        int*       delay;
        int        total_num_broadcast;
        double     total_bus_contention;
        int*       home_stat;
        std::vector<CacheLevel> cache_level;
        Cache***   cache;
        Cache**    directory_cache;
        Cache*     tlb_cache;
        pthread_mutex_t** cache_lock;
        pthread_mutex_t*  directory_cache_lock;
        bool**     cache_init_done;
        bool*      directory_cache_init_done;
        PageTable  page_table;
        Network    network;
        Dram       dram;
        Bus**      bus;
        Bus*       directory_cache_bus;
};

#endif // SYSTEM_H

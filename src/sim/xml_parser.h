//===========================================================================
// xml_parser.h 
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

#ifndef XML_PARSER_H
#define XML_PARSER_H

#include <string>
#include <inttypes.h>
#include <fstream>
#include <sstream>
#include <assert.h>
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/xpath.h>


struct XmlCache
{
    int         level = 0;
    int         share = 0;
    int         access_time = 0;
    uint64_t    size = 0;
    uint64_t    block_size = 0;
    uint64_t    num_ways = 0;
};

struct XmlNetwork
{
    int data_width = 0;
    int header_flits = 0;
    int net_type = 0;
    uint64_t router_delay = 0;
    uint64_t link_delay = 0;
    uint64_t inject_delay = 0;
};

struct XmlBus
{
    int bandwidth = 0;
    bool unlim_bw = false;
    int ctrl_pkt_len = 0;
    int data_pkt_len = 0;
};

struct XmlSys
{
    int         sys_type = 0;
    int         protocol_type = 0;
    int         protocol = 0;
    int         max_num_sharers = 0;
    int         page_size = 0;
    int         tlb_enable = 0;
    int         shared_llc = 0;
    int         verbose_report = 0;
    double      cpi_nonmem = 0;
    int         dram_access_time = 0;
    int         num_levels = 0;
    int         num_cores = 0;
    double      freq = 0;
    int         page_miss_delay = 0;
    XmlNetwork  network;
    XmlBus      bus;
    XmlCache    directory_cache;
    XmlCache    tlb_cache;
    XmlCache*   cache = NULL;
};

struct XmlSim
{
    int        max_msg_size = 0;
    int        num_cons_threads = 0;
    int        num_prod_threads = 0;
    int        thread_sync_interval = 0;
    int        syscall_cost = 0;
    XmlSys     sys; 
};


class XmlParser 
{
    public:
        XmlParser(const char *docname);
        const XmlSim&   getXmlSim() const;
        ~XmlParser();
        bool isOk() { return ok; };
    private:
        XmlParser() = delete;
        bool getDoc(const char *docname);
        xmlXPathObjectPtr getNodeSet(xmlChar *xpath);
        bool parseCache(); 
        bool parseBus(); 
        bool parseNetwork(); 
        bool parseDirectoryCache(); 
        bool parseTlbCache(); 
        bool parseSys(); 
        bool parseSim(); 
        XmlSim    xml_sim;
        xmlDocPtr doc;
        bool ok = false;
};

#endif  // XML_PARSER_H

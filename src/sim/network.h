//===========================================================================
// network.h 
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

#ifndef NETWORK_H
#define NETWORK_H

#include <string>
#include <inttypes.h>
#include <map>
#include <set>
#include <vector>
#include <thread> 
#include <mutex>
#include "link.h"
#include "cache.h"

enum Direction 
{
    EAST = 0,
    WEST = 1,
    NORTH = 2,
    SOUTH = 3,
    UP = 4,
    DOWN = 5
};

enum NetworkType
{
    MESH_2D = 0,
    MESH_3D = 1
};

struct Coord
{
    int x = 0;
    int y = 0;
    Coord(int _x, int _y) : x(_x), y(_y) {};
    Coord() = default;
};


class Network
{
public:
    ~Network();
    Network(int num_nodes_in, const XmlNetwork& xml_net, bool verbose_report);
    uint64_t transmit(int sender, int receiver, int data_len, uint64_t timer);
    Coord getLoc(int node_id); 
    int getNodeId(Coord loc);
    Link* getLink(Coord node_id, Direction direction);
    void report(std::ofstream& result_ofstream);

    const int num_nodes;
    const int net_width;
    const int header_flits;
private:
    Network();
    Link*** link;
    const bool verbose_report;
    const int data_width;
    const uint64_t router_delay;
    const uint64_t link_delay;
    const uint64_t inject_delay;
    uint64_t num_access = 0;
    uint64_t total_delay = 0;
    uint64_t total_router_delay = 0;
    uint64_t total_link_delay = 0;
    uint64_t total_inject_delay = 0;
    uint64_t total_distance = 0;
    double avg_delay = 0;
    std::mutex local_mutex;
};


#endif //NETWORK_H

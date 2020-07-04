//===========================================================================
// network.cpp implements 2D mesh network. Only link contention is 
// calculated by queue model. The routing algorithm is horizontal direction first. 
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
#include <cmath>
#include <inttypes.h>
#include <assert.h>

#include "network.h"
#include "common.h"


using namespace std;

Network::Network(int num_nodes_in, const XmlNetwork& xml_net, bool _verbose_report) : 
    num_nodes(num_nodes_in),
    header_flits(xml_net.header_flits),
    data_width(xml_net.data_width),
    router_delay(xml_net.router_delay),
    link_delay(xml_net.link_delay),
    inject_delay(xml_net.inject_delay),
    net_width((int)ceil(sqrt(num_nodes))), 
    verbose_report(_verbose_report)
{
    link = new Link** [net_width-1];
    for (int i = 0; i < net_width-1; i++) {
        link[i] = new Link* [2*net_width];
        for (int j = 0; j < 2*net_width; j++) {
            link[i][j] = new Link(xml_net.link_delay);
        }
    }
}

//Calculate packet communication latency
uint64_t Network::transmit(int sender, int receiver, int data_len, uint64_t timer)
{
    if(sender == receiver) {
        return 0;
    }
    assert(sender >= 0 && sender < num_nodes);
    assert(receiver >= 0 && receiver < num_nodes);
    int packet_len = header_flits + (int)ceil((double)data_len/data_width); 
    Coord loc_sender = getLoc(sender); 
    Coord loc_receiver = getLoc(receiver); 
    Coord loc_cur = loc_sender;
    Direction direction;
    uint64_t    local_timer = timer;
    uint64_t    local_distance = 0;
    Link* link_cur = NULL;

    //Injection delay
    local_timer += inject_delay;
    
    direction = loc_receiver.x > loc_cur.x ? EAST : WEST;
    local_distance += abs(loc_receiver.x - loc_cur.x);
    while (loc_receiver.x != loc_cur.x) {
        local_timer += router_delay;
        link_cur = getLink(loc_cur, direction);
        assert(link_cur != NULL);
        local_timer += link_cur->access(local_timer, packet_len);
        loc_cur.x = (direction == EAST) ? (loc_cur.x + 1) : (loc_cur.x - 1);
    }

    direction = loc_receiver.y > loc_cur.y ? SOUTH : NORTH;
    local_distance += abs(loc_receiver.y - loc_cur.y);
    while (loc_receiver.y != loc_cur.y) {
        local_timer += router_delay;
        link_cur = getLink(loc_cur, direction);
        assert(link_cur != NULL);
        local_timer += link_cur->access(local_timer, packet_len);
        loc_cur.y = (direction == SOUTH) ? (loc_cur.y + 1) : (loc_cur.y - 1);
    }

    local_timer += router_delay;
    //Pipe delay 
    local_timer += packet_len - 1; 
 

    local_mutex.lock();
    num_access++;
    total_delay += local_timer - timer;
    total_router_delay += (local_distance+1) * router_delay;
    total_link_delay += local_timer - timer - (local_distance+1)*router_delay - (packet_len-1) - inject_delay;
    total_inject_delay += inject_delay;
    total_distance += local_distance;
    local_mutex.unlock();
    return (local_timer - timer);
}


Coord Network::getLoc(int node_id)
{
    return Coord(node_id % net_width, node_id / net_width);
}

int Network::getNodeId(Coord loc)
{
    return loc.x + loc.y * net_width;
}


//Find the pointer to the link of a specific location
Link* Network::getLink(Coord node_id, Direction direction)
{
    Coord link_id;    
    switch(direction) {
        case EAST:
           link_id.x = node_id.x; 
           link_id.y = node_id.y;
           break;
        case WEST:
           link_id.x = node_id.x - 1; 
           link_id.y = node_id.y;
           break;
        case NORTH:
           link_id.x = node_id.y - 1; 
           link_id.y = node_id.x + net_width;
           break;
        case SOUTH:
           link_id.x = node_id.y; 
           link_id.y = node_id.x + net_width;
           break;
        default:
           link_id.x = node_id.x; 
           link_id.y = node_id.y;
           break;
    }
    if((link_id.x >= 0) && (link_id.x < net_width-1)
     &&(link_id.y >= 0) && (link_id.y < 2*net_width)) {
        return link[link_id.x][link_id.y];
    }
    else {
        cerr <<"# of nodes: "<<num_nodes<<endl;
        cerr <<"Network width: "<<net_width<<endl;
        cerr <<"Direction: "<<direction<<endl;
        cerr <<"Node coordinate: ("<<node_id.x<<", "<<node_id.y<<")\n";
        cerr <<"Link coordinate: ("<<link_id.x<<", "<<link_id.y<<")\n";
        cerr <<"Can't find correct link id!\n";
        return NULL;
    }
}


void Network::report(ofstream& result_ofstream)
{

    avg_delay = (double)total_delay / num_access; 
    result_ofstream << "Network Stat:\n";
    result_ofstream << "# of accesses: " << num_access <<endl;
    result_ofstream << "Total network communication distance: " << total_distance <<endl;
    result_ofstream << "Total network delay: " << total_delay <<endl;
    result_ofstream << "Total router delay: " << total_router_delay <<endl;
    result_ofstream << "Total link delay: " << total_link_delay <<endl;
    result_ofstream << "Total inject delay: " << total_inject_delay <<endl;
    result_ofstream << "Total contention delay: " << total_link_delay - total_distance*link_delay <<endl;
    result_ofstream << "Average network delay: " << avg_delay <<endl <<endl;

    if (verbose_report) {
        result_ofstream << "Home Occupation:\n";

        result_ofstream << "Allocated home locations in 2D coordinates:" << endl;
        for (int i = 0; i < num_nodes; i++) {
            Coord loc = getLoc(i); 
            result_ofstream << "("<<loc.x<<", "<<loc.y<<")\n";
        }
        result_ofstream << endl;
    }
    result_ofstream << endl;
}

Network::~Network()
{
    for (int i = 0; i < net_width-1; i++) {
        for (int j = 0; j < 2*net_width; j++) {
            delete link[i][j];
        }
        delete [] link[i];
    } 
    delete [] link;
}

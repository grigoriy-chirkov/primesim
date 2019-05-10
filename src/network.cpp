//===========================================================================
// network.cpp implements 2D and 3D mesh network. Only link contention is 
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
#include <string>
#include <cstring>
#include <vector>
#include <inttypes.h>
#include <assert.h>

#include "network.h"
#include "common.h"


using namespace std;

/////////////////////////////////////////////////////////////////
//                  Common Functions
/////////////////////////////////////////////////////////////////


bool Network::init(int num_nodes_in, XmlNetwork* xml_net)
{
    num_nodes = num_nodes_in;
    initXML(xml_net);

    switch (net_type) {
        case MESH_3D:
            init3DMesh();
            break;
        case MESH_2D:
            init2DMesh();
            break;
        case OMEGA:
            initOmega();
            break;
        case TREE:
            initTree();
            break;
        case BUTTERFLY:
            initButtefly();
            break;
        case CCC:
            initCCC();
            break;
        default:
            cerr << "Not supported network type: " << net_type << endl;
            exit(1);
    }

    num_access = 0;
    total_delay = 0;
    total_router_delay = 0;
    total_link_delay = 0;
    total_inject_delay = 0;
    total_distance = 0;
    avg_delay = 0;
    pthread_mutex_init(&mutex, NULL);

    return true;
}

void Network::initXML(XmlNetwork* xml_net)
{
    assert(xml_net != nullptr);

    net_type = xml_net->net_type;
    header_flits = xml_net->header_flits;
    data_width = xml_net->data_width;
    router_delay = xml_net->router_delay;
    link_delay = xml_net->link_delay;
    inject_delay = xml_net->inject_delay;
}


//Calculate packet communication latency
uint64_t Network::transmit(int sender, int receiver, int data_len, uint64_t timer)
{    
    if(sender == receiver) {
        return 0;
    }

    int packet_len = header_flits + (int)ceil((double)data_len/data_width); 

    uint64_t    local_timer = timer;
    uint64_t    distance = 0;

    //Injection delay
    local_timer += inject_delay;
    
    while (sender != receiver) {
        Link* link = getNextLink(sender, receiver);
        assert(link != nullptr);
        local_timer += link->access(local_timer, packet_len);
        auto ids = link->get_ids();
        //cout << ids.first << " " << ids.second << endl;
        if (ids.first == sender) {
            sender = ids.second;
        } else {
            sender = ids.first;
        }
        distance++;
    }

    local_timer += router_delay;
    //Pipe delay 
    local_timer += packet_len - 1; 
 
    // stats
    pthread_mutex_lock(&mutex);
    num_access++;
    total_delay += local_timer - timer;
    total_router_delay += (distance+1) * router_delay;
    total_link_delay += local_timer - timer - (distance+1)*router_delay - (packet_len-1) - inject_delay;
    total_inject_delay += inject_delay;
    total_distance += distance;
    pthread_mutex_unlock(&mutex);
    return (local_timer - timer);
}

Link* Network::getNextLink(int sender, int receiver)
{
    assert(receiver >= 0 && receiver < num_nodes);

    switch (net_type) {
        case MESH_3D:
            return getNextLink3DMesh(sender, receiver);
        case MESH_2D:
            return getNextLink2DMesh(sender, receiver);
        case OMEGA:
            return getNextLinkOmega(sender, receiver);
        case TREE:
            return getNextLinkTree(sender, receiver);
        case BUTTERFLY:
            return getNextLinkButterfly(sender, receiver);
        case CCC:
            return getNextLinkCCC(sender, receiver);
        default:
            cerr << "Not supported network type: " << net_type << endl;
            return nullptr;
    }

}

int Network::getNumNodes()
{
    return num_nodes;
}

int Network::getNetType()
{
    return net_type;
}

int Network::getNetWidth()
{
    return net_width;
}

int Network::getHeaderFlits()
{
    return header_flits;
}

void Network::report(ofstream* result)
{
    assert(result != nullptr);

    avg_delay = (double)total_delay / num_access; 
    *result << "Network Stat:\n";
    *result << "# of accesses: " << num_access <<endl;
    *result << "Total network communication distance: " << total_distance <<endl;
    *result << "Total network delay: " << total_delay <<endl;
    *result << "Total router delay: " << total_router_delay <<endl;
    *result << "Total link delay: " << total_link_delay <<endl;
    *result << "Total inject delay: " << total_inject_delay <<endl;
    *result << "Total contention delay: " << total_link_delay - total_distance*link_delay <<endl;
    *result << "Average network delay: " << avg_delay <<endl <<endl;
}

Network::~Network()
{
    switch (net_type) {
        case MESH_3D:
            destroy3DMesh();
            break;
        case MESH_2D:
            destroy2DMesh();
            break;
        case OMEGA:
            destroyOmega();
            break;
        case TREE:
            destroyTree();
            break;
        case BUTTERFLY:
            destroyButterfly();
            break;
        case CCC:
            destroyCCC();
            break;
        default:
            cerr << "Not supported network type: " << net_type << endl;
            exit(1);
    }

    pthread_mutex_destroy(&mutex);
}


/////////////////////////////////////////////////////////////////
//                  Topology-specific Functions
/////////////////////////////////////////////////////////////////

/*
 *  2D mesh
 */


void Network::init2DMesh()
{
    assert(net_type == MESH_2D);
    net_width = (int)ceil(sqrt(num_nodes));
    link = new Link** [net_width-1];
    assert(link != nullptr);
    for (int i = 0; i < net_width-1; i++) {
        link[i] = new Link* [2*net_width];
        assert(link[i] != nullptr);
        for (int j = 0; j < 2*net_width; j++) {
            link[i][j] = new Link();
            assert(link[i][j] != nullptr);
            if (j < net_width) {
                link[i][j]->init(link_delay, getNodeId2D(i, j), getNodeId2D(i+1, j));
            } else {
                link[i][j]->init(link_delay, getNodeId2D(j-net_width, i), getNodeId2D(j-net_width, i+1));
            }
        }
    }
}


Link* Network::getNextLink2DMesh(int sender, int receiver)
{
    assert(net_type == MESH_2D);


    Coord loc_sender = getLoc2D(sender); 
    Coord loc_receiver = getLoc2D(receiver); 
    Direction direction;

    if (loc_receiver.x != loc_sender.x) {
        direction = loc_receiver.x > loc_sender.x ? EAST : WEST;
    } else if (loc_receiver.y != loc_sender.y) {
        direction = loc_receiver.y > loc_sender.y ? SOUTH : NORTH;
    } else {
        cerr << "Node is asked to find link to itself" << endl;
        return nullptr;
    }

    return getLinkFromDir2D(loc_sender, direction);
}

Link* Network::getLinkFromDir2D(Coord node_id, Direction direction) {
    assert(net_type == MESH_2D);
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
    // Optional: Make sure that the link coordinates are correct.
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
        return nullptr;
    }
}

Coord Network::getLoc2D(int node_id)
{
    assert(net_type == MESH_2D);
    Coord loc;
    loc.x = node_id % net_width;
    loc.y = node_id / net_width;
    loc.z = 0;
    return loc;
}

int Network::getNodeId2D(Coord loc)
{
    assert(net_type == MESH_2D);
    return loc.x + loc.y * net_width;
}

int Network::getNodeId2D(int x, int y)
{
    assert(net_type == MESH_2D);
    return x + y * net_width;
}

void Network::destroy2DMesh()
{
    assert(net_type == MESH_2D);
    for (int i = 0; i < net_width-1; i++) {
        for (int j = 0; j < 2*net_width; j++) {
            delete link[i][j];
        }
        delete [] link[i];
    } 
    delete [] link;
}

/*
 *  3D mesh
 */

void Network::init3DMesh()
{
    assert(net_type == MESH_3D);
    net_width = (int)ceil(cbrt(num_nodes));
    link = new Link** [net_width-1];
    for (int i = 0; i < net_width-1; i++) {
        link[i] = new Link* [net_width];
        for (int j = 0; j < net_width; j++) {
            link[i][j] = new Link [3*net_width];
            for (int k = 0; k < 3*net_width; k++) {
                if (k < net_width) {
                    link[i][j][k].init(link_delay, getNodeId3D(i, j, k), getNodeId3D(i+1, j, k));
                } else if (net_width <= k && k < 2*net_width) {
                    link[i][j][k].init(link_delay, getNodeId3D(k-net_width, i, j), getNodeId3D(k-net_width, i+1, j));
                } else {
                    link[i][j][k].init(link_delay, getNodeId3D(j, k-2*net_width, i), getNodeId3D(j, k-2*net_width, i+1));
                }

            }
        }
    }
}

Link* Network::getNextLink3DMesh(int sender, int receiver)
{
    assert(net_type == MESH_3D);

    Coord loc_sender = getLoc3D(sender); 
    Coord loc_receiver = getLoc3D(receiver); 
    Direction direction;

    if (loc_receiver.x != loc_sender.x) {
        direction = loc_receiver.x > loc_sender.x ? EAST : WEST;
    } else if (loc_receiver.y != loc_sender.y) {
        direction = loc_receiver.y > loc_sender.y ? SOUTH : NORTH;
    } else if (loc_receiver.z != loc_sender.z) {
        direction = loc_receiver.z > loc_sender.z ? UP : DOWN;
    } else {
        cerr << "Node is asked to find link to itself" << endl;
        exit(1);
    }

    return getLinkFromDir3D(loc_sender, direction);
}

Link* Network::getLinkFromDir3D(Coord node_id, Direction direction) 
{
    assert(net_type == MESH_3D);
    Coord link_id;

    switch(direction) {
        case EAST:
           link_id.x = node_id.x; 
           link_id.y = node_id.y;
           link_id.z = node_id.z;
           break;
        case WEST:
           link_id.x = node_id.x - 1; 
           link_id.y = node_id.y;
           link_id.z = node_id.z;
           break;
        case NORTH:
           link_id.x = node_id.y - 1; 
           link_id.y = node_id.z;
           link_id.z = node_id.x + net_width;
           break;
        case SOUTH:
           link_id.x = node_id.y; 
           link_id.y = node_id.z;
           link_id.z = node_id.x + net_width;
           break;
        case UP:
           link_id.x = node_id.z; 
           link_id.y = node_id.x;
           link_id.z = node_id.y + 2 * net_width;
           break;
        case DOWN:
           link_id.x = node_id.z - 1; 
           link_id.y = node_id.x;
           link_id.z = node_id.y + 2 * net_width;
           break;
        default:
           link_id.x = node_id.x; 
           link_id.y = node_id.y;
           link_id.z = node_id.z;
           break;
    // Optional: Make sure that the link coordinates are correct.
    }
    if((link_id.x >= 0) && (link_id.x < net_width-1)
     &&(link_id.y >= 0) && (link_id.y < net_width)
     &&(link_id.z >= 0) && (link_id.y < 3*net_width)) {
        return &link[link_id.x][link_id.y][link_id.z];
    }
    else {
        return nullptr;
    }
}

int Network::getNodeId3D(Coord loc)
{
    assert(net_type == MESH_3D);
    return loc.x + loc.y * net_width + loc.z * net_width * net_width;
}

int Network::getNodeId3D(int x, int y, int z)
{
    assert(net_type == MESH_3D);
    return x + y * net_width + z * net_width * net_width;
}

Coord Network::getLoc3D(int node_id)
{
    assert(net_type == MESH_3D);
    Coord loc;
    loc.x = (node_id % (net_width * net_width)) % net_width;
    loc.y = (node_id % (net_width * net_width)) / net_width;
    loc.z = node_id / (net_width * net_width);
    return loc;
}

void Network::destroy3DMesh()
{
    assert(net_type == MESH_3D);
    for (int i = 0; i < net_width-1; i++) {
        for (int j = 0; j < net_width; j++) {
            delete [] link[i][j];
        }
        delete [] link[i];
    } 
    delete [] link;
}


// Modify: Change for more networks; based on Coord struct


/*
 *  Omega
 */

void Network::initOmega()
{
    assert(net_type == OMEGA);
    assert(is2Power(num_nodes));

    link = new Link** [log2(num_nodes) + 1];
    assert(link != nullptr);

    for (int i = 0; i < log2(num_nodes) + 1; i++) {
        link[i] = new Link* [num_nodes];
        assert(link[i] != nullptr);
    
        for (int j = 0; j < num_nodes; j++) {
            link[i][j] = new Link();
            assert(link[i][j] != nullptr);
            if (i == 0) {
                link[i][j]->init(link_delay, j, j%(num_nodes/2) + num_nodes);
            } else if (i == log2(num_nodes)) {
                link[i][j]->init(link_delay, j/2 + num_nodes/2 * (1+log2(num_nodes)), j);
            } else {
                link[i][j]->init(link_delay, j/2 + num_nodes/2 * (1+i), j%(num_nodes/2) + num_nodes/2 * (i+2));
            }
            //cout << i << " " << j << " " << link[i][j]->get_ids().first << " " << link[i][j]->get_ids().second << endl;

        }
    }
}

int Network::log2(int n)
{
    int res = 0;
    if (n == 0) {
        return 0;
    }
    while(n != 1) {
        n = n >> 1;
        res++;
    }
    return res;
}

bool Network::is2Power(int n)
{
    return bool(n ^ (n - 1));
}

Link* Network::getNextLinkOmega(int sender, int receiver)
{
    //cout << sender << " " << receiver << endl;
    assert(net_type == OMEGA);
    if (sender == receiver) {
        return nullptr;
    }
    if (sender < num_nodes) {
        return link[0][sender];
    }
    int column = (sender - num_nodes) / (num_nodes / 2) + 1;
    int row = sender - num_nodes - (column - 1) * num_nodes/2;
    int direction = (receiver >> (log2(num_nodes) - column)) & 0x1;
    //cout << column << " " << row << " " << direction << endl;
    //cout << link[column][2 * row + direction]->get_ids().first << " " << link[column][2 * row + direction]->get_ids().second << endl;
    return link[column][2 * row + direction];
}

void Network::destroyOmega()
{
    assert(net_type == OMEGA);
    for (int i = 0; i < log2(num_nodes) + 1; i++) {
        for (int j = 0; j < num_nodes; j++) {
            delete link[i][j];
        }
        delete [] link[i];
    } 
    delete [] link;
}

/*
 *  Butterfly
 */

void Network::initButtefly()
{
    assert(net_type == BUTTERFLY);
    assert(is2Power(num_nodes));
    int lognum = log2(num_nodes);
    link = new Link** [lognum];
    assert(link != nullptr);
    for (int i = 0; i < lognum; i++) {
        link[i] = new Link* [2*num_nodes];
        assert(link[i] != nullptr);    
        for (int j = 0; j < num_nodes; j++) {
            link[i][2*j] = new Link();
	    link[i][2*j+1] = new Link();
            assert(link[i][2*j] != nullptr);
            assert(link[i][2*j+1] != nullptr);
            if(i==(lognum-1)){
                link[i][2*j]->init(link_delay,i*num_nodes + j, j & ~1);
	        link[i][2*j+1]->init(link_delay,i*num_nodes + j, j | 1); 
            }
            else {
	        link[i][2*j]->init(link_delay,i*num_nodes + j, (i+1)*num_nodes + (j & ~(1<<(lognum - i - 1))));
	        link[i][2*j+1]->init(link_delay,i*num_nodes + j, (i+1)*num_nodes + (j | (1<<(lognum - i - 1))));            
            }
        }
    }
    
}

Link* Network::getNextLinkButterfly(int sender, int receiver)
{
    assert(net_type == BUTTERFLY);
    if (sender == receiver) {
        return nullptr;
    }
    int column = sender/num_nodes;
    int row = sender%num_nodes;
    int direction = (receiver >> (log2(num_nodes) - column - 1)) & 1;
    return link[column][2 * row + direction];
}

void Network::destroyButterfly()
{
    assert(net_type == BUTTERFLY);
    for (int i = 0; i < log2(num_nodes); i++) {
        for (int j = 0; j < 2*num_nodes; j++) {
            delete link[i][j];
        }
        delete [] link[i];
    } 
    delete [] link;
}

/*
 *  Tree
 */

// Recursively finds the path from root to Node with number nodeNum.
bool Network::getPathRootNode(Node* root, vector<int>& path, int nodeNum)
{
    path.push_back(root->num);

    // Current node is nodeNum
    if(root->num == nodeNum)
        return true;
    // Recursively checks if nodeNum is in either the left or right subtree of the current node.
    if(getPathRootNode(root->left, path, nodeNum) || getPath(root->right, path, nodeNum))
        return true;
    // nodeNum is not in this subtree; remove current node from path.
    path.pop_back();
        return false;
}

vector<int> Network::getPathNodeNode(Node* root, int nodeNum1, int nodeNum2)
{
    // Find paths from root node to both target nodes.
    vector<int> path1;
    vector<int> path2;
    vector<int> pathNew;
    getPath(root, path1, nodeNum1);
    getPath(root, path2, nodeNum2);

    // Get intersection point
    int intersection = -1;
    int i = 0;
    int j = 0;
    while (i != path1.size() || j != path2.size())
    {
        if ((i == j) && (path1[i] == path2[j]))
        {
            i++;
            j++;
        }
        else 
        {
            intersection = i - 1;
            break;
        }
    }

    // Returns path from nodeNum1 to nodeNum2
    for (int i = path1.size() - 1; i > intersection - 1; i--)
    {
        pathNew.push_back(path1[i]);
    }
    for (int i = intersection + 1; i < path2.size(); i++)
    {
        pathNew.push_back(path2[i]);
    }
    return pathNew;
}


void Network::initTree()
{
    // Your code here
}

Link* Network::getNextLinkTree(int sender, int receiver)
{
    // Your code here
    return nullptr;
}

void Network::destroyTree()
{
    // Your code here
}

/*
 *  CCC
 */

void Network::initCCC()
{
    // Your code here
}

Link* Network::getNextLinkCCC(int sender, int receiver)
{
    // Your code here
    return nullptr;
}

void Network::destroyCCC()
{
    // Your code here
}

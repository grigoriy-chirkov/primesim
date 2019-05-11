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
#include <queue>
#include <pthread.h> 
#include "link.h"
#include "cache.h"

class Link;

enum NetworkType
{
    MESH_2D = 0,
    MESH_3D = 1, 
    OMEGA = 2, 
    TREE = 3, 
    BUTTERFLY = 4, 
    CCC = 5
};


// Mesh types
enum Direction 
{
    EAST = 0,
    WEST = 1,
    NORTH = 2,
    SOUTH = 3,
    UP = 4,
    DOWN = 5
};

typedef struct Coord
{
    int x;
    int y;
    int z;
    Coord(int _x, int _y, int _z) : x(_x), y(_y), z(_z) {};
    Coord() {};
} Coord;

// Node data structure (used for tree network type)
typedef struct Node 
{
  int num;
  Node *left, *right;
} Node;

class Network
{
    public:
       // Common
       bool init(int num_nodes_in, XmlNetwork* xml_net);
       void initXML(XmlNetwork* xml_net);
       uint64_t transmit(int sender, int receiver, int data_len, uint64_t timer);
       Link* getNextLink(int sender, int receiver);
       int getNumNodes();
       int getNetType();
       int getNetWidth();
       int getHeaderFlits();
       void report(ofstream* result);
       ~Network();

       // 2D mesh
       void init2DMesh();
       Coord getLoc2D(int node_id); 
       int getNodeId2D(Coord loc);
       int getNodeId2D(int x, int y);
       Link* getLinkFromDir2D(Coord node_id, Direction direction);
       Link* getNextLink2DMesh(int sender, int receiver);
       void destroy2DMesh();

       // 3D mesh
       void init3DMesh();
       Coord getLoc3D(int node_id); 
       int getNodeId3D(Coord loc);
       int getNodeId3D(int x, int y, int z);
       Link* getLinkFromDir3D(Coord node_id, Direction direction);
       Link* getNextLink3DMesh(int sender, int receiver);
       void destroy3DMesh();

       // Omega
       void initOmega();
       Link* getNextLinkOmega(int sender, int receiver);
       int  log2(int n);
       bool is2Power(int n);
       void destroyOmega();

       // Butterfly
       void initButtefly();
       Link* getNextLinkButterfly(int sender, int receiver);
       void destroyButterfly();

       // Tree
       Node* createNode(int num);
       Node* insertNode(Node* root, int nodeNum, queue<Node *>& q);
       Node* createTree();
       bool getPathRootNode(Node* root, vector<int>& path, int nodeNum);
       vector<int> getPathNodeNode(Node* root, int nodeNum1, int nodeNum2);
       void initTree();
       Link* getNextLinkTree(int sender, int receiver);
       void destroyTree();

       // CCC
       void initCCC();
       Link* getNextLinkCCC(int sender, int receiver);
       void destroyCCC();

   private:
       int net_type;
       int num_nodes;
       int data_width;
       int header_flits;
       uint64_t router_delay;
       uint64_t link_delay;
       uint64_t inject_delay;
       Link*** link;

       // 2D, 3D mesh
       int net_width;

       // Stats
       uint64_t num_access;
       uint64_t total_delay;
       uint64_t total_router_delay;
       uint64_t total_link_delay;
       uint64_t total_inject_delay;
       uint64_t total_distance;
       double avg_delay;
       pthread_mutex_t mutex;      
};


#endif //NETWORK_H

#ifndef NETWORK_MESH_MD1_H_
#define NETWORK_MESH_MD1_H_

#include "g_std/g_unordered_map.h"
#include "g_std/g_multimap.h"
#include "g_std/g_string.h"
#include "locks.h"
#include "network.h"

#include <tuple>

class MeshMD1Network : public Network {
public:
    MeshMD1Network(const char* config);
    uint32_t getRTT(const char* src, const char* dst, const bool isInvalidateMsg) override;

private:
    struct LinkId {
        int x1;
        int y1;
        int x2;
        int y2;

        bool operator<(const LinkId& other) const {
            return std::tie(x1, y1, x2, y2) < std::tie(other.x1, other.y1, other.x2, other.y2);
        }
    };

    struct LinkState {
        double queueDelay = 0.0;     // cycles of expected waiting time
        double serviceTime = 1.0;    // hardcoded: cycles per flit
        uint64_t flitsThisWindow = 0;
        uint64_t msgsThisWindow = 0;
    };

    int xDim = 0, yDim = 0;
    uint32_t hopX = 1, hopY = 1;

    struct Coord {
        int x{ -1 };
        int y{ -1 };
    };
    // placement
    g_map<g_string, Coord> place;

    // fix link delay spec
    g_map<g_string, uint32_t> linkDelay;

    bool ruleL1toL2 = false;
    uint32_t ruleL1toL2Delay = 0;

    bool rulePrefetch = true;
    uint32_t rulePrefetchDelay = 0;

    g_map<LinkId, LinkState> linkStates;
    uint64_t lastQueueUpdatePhase = 0;
    lock_t statsLock;

    void maybeUpdateQueuesLocked();
    double accountHopLocked(int x1, int y1, int x2, int y2, uint32_t flits);
    static bool isPrefetchNode(const g_string& id);
    static g_string prefetchBaseName(const g_string& id);
};

  // general rules



#endif // NETWORK_MESH_MD1_H_

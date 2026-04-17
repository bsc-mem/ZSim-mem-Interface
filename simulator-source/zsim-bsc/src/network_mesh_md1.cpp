#include "zsim.h"
#include "log.h"
#include "network_mesh_md1.h"
#include "g_std/g_string.h"

#include <cstdlib> // for std::abs with integers
#include <cmath>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <utility>

static inline std::string stripComment(const std::string& s) {
    auto pos = s.find('#');
    return (pos == std::string::npos) ? s : s.substr(0, pos);
}

static inline void trim(std::string& s) {
    auto notspace = [](int ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
}

static inline bool parseCoreIdx(const g_string& id, const char* pfx, int& outIdx) {
    const char* s = id.c_str();
    const size_t L = std::strlen(pfx);
    if (std::strncmp(s, pfx, L) != 0) return false;
    char* end = nullptr;
    long v = std::strtol(s + L, &end, 10);
    if (!end || *end != '\0') return false;
    outIdx = static_cast<int>(v);
    return true;
}

static inline g_string key(const g_string& a, const g_string& b) { return a + '\t' + b; }

bool MeshMD1Network::isPrefetchNode(const g_string& id) {
    std::string s(id.c_str());
    return s.find("pref") != std::string::npos;
}

g_string MeshMD1Network::prefetchBaseName(const g_string& id) {
    std::string s(id.c_str());
    auto pos = s.rfind("-pref");
    if (pos != std::string::npos) {
        return g_string(s.substr(0, pos).c_str());
    }
    return g_string("");
}

MeshMD1Network::MeshMD1Network(const char* filename) {
    futex_init(&statsLock);
    std::ifstream in(filename);
    if (!in) panic("cannot open NoC file: %s", filename);

    std::string line;
    uint32_t lineno = 0;

    while (std::getline(in, line)) {
        lineno++;
        line = stripComment(line);
        trim(line);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd == "mesh") {
            if (!(iss >> xDim >> yDim)) panic("mesh line needs: mesh <x> <y>");
            if (xDim <= 0 || yDim <= 0) panic("mesh dims must be positive");
        } else if (cmd == "hop") {
            if (!(iss >> hopX >> hopY)) panic("hop line needs: hop <x_hop> <y_hop>");
            if (hopX == 0 || hopY == 0) panic("hop must be non-zero");
        } else if (cmd == "place") {
            g_string id;
            int x, y;
            if (!(iss >> id >> x >> y)) panic("place line needs: place <id> <x> <y>");
            if (x < 0 || y < 0 || x >= xDim || y >= yDim) panic("place coords out of mesh");
            if (place.count(id)) panic("duplicate placement: %s", id.c_str());
            place[id] = Coord{ x, y };
        } else if (cmd == "rule") {
            std::string name;
            uint32_t d;
            if (!(iss >> name >> d)) panic("rule needs: rule <name> <delay>");
            if (name == "l1_to_l2") {
                ruleL1toL2 = true;
                ruleL1toL2Delay = d;
            } else if (name == "prefetch") {
                rulePrefetch = true;
                rulePrefetchDelay = d;
            } else {
                panic("unknown rule: %s", name.c_str());
            }
        } else if (cmd == "link") {
            g_string a, b, dir;
            uint32_t d;
            if (!(iss >> a >> b >> d)) panic("link needs: link <a> <b> <delay> [uni]");
            if (iss >> dir && dir == "uni") {
                linkDelay[key(a, b)] = d;
            } else {
                linkDelay[key(a, b)] = d;
                linkDelay[key(b, a)] = d;
            }
        } else if (cmd == "override") {
            panic("NOT IMPLEMENTED YET");
            // std::string a, b, dir;
            // uint32_t d;
            // if (!(iss >> a >> b >> d)) panic("override needs: override <a> <b> <delay> [uni]");
            // if (iss >> dir && dir == "uni") {
            //     overrideDelay_[key(a, b)] = d;
            // } else {
            //     overrideDelay_[key(a, b)] = d;
            //     overrideDelay_[key(b, a)] = d;
            // }
        } else {
            panic("unknown directive at line %u : %s", lineno, cmd.c_str());
        }
    }

    if (xDim == 0 || yDim == 0) panic("missing mesh directive");
    if (hopX == 0 || hopY == 0) panic("missing hop directive");

    lastQueueUpdatePhase = 0;
}

uint32_t MeshMD1Network::getRTT(const char* src, const char* dst, bool isInvalidateMsg) {
    const g_string a(src), b(dst);

    // check if fix lat is set from a to b
    auto it = linkDelay.find(key(a, b));
    if (it != linkDelay.end()) return it->second;

    if (rulePrefetch) {
        if (isPrefetchNode(a) || isPrefetchNode(b)) {
            return rulePrefetchDelay;
        }
        g_string baseA = prefetchBaseName(a);
        g_string baseB = prefetchBaseName(b);
        if (!baseA.empty() && baseA == b) return rulePrefetchDelay;
        if (!baseB.empty() && baseB == a) return rulePrefetchDelay;
    }

    // check if L1 <=> L2 transaction
    if (ruleL1toL2) {
        int n1 = -1, n2 = -1, m1 = -1, m2 = -1;
        bool aIsL1 = parseCoreIdx(a, "l1i-", n1) || parseCoreIdx(a, "l1d-", n1);
        bool bIsL1 = parseCoreIdx(b, "l1i-", n2) || parseCoreIdx(b, "l1d-", n2);
        bool aIsL2 = parseCoreIdx(a, "l2-", m1);
        bool bIsL2 = parseCoreIdx(b, "l2-", m2);
        if ((aIsL1 && bIsL2 && n1 == m2) || (bIsL1 && aIsL2 && n2 == m1)) return ruleL1toL2Delay;
    }

    auto getCoordLambda = [&](const char* key) {
        auto it = place.find(key);
        if (it == place.end()) {
            panic("missing placement for %s", key);
        }
        return it->second;
    };

    const auto& srcCoord = getCoordLambda(src);
    const auto& dstCoord = getCoordLambda(dst);
    // FIXME: hardcoded flit size
    const uint32_t flitNum =
        isInvalidateMsg ? 1u : static_cast<uint32_t>(std::ceil(static_cast<double>(zinfo->lineSize) / 32.0));

    Coord curCoord = srcCoord;
    const bool doesTurn = srcCoord.x != dstCoord.x && srcCoord.y != dstCoord.y;
    double totalQWait = 0;
    uint64_t totalHopLat = 0;

    // update NoC queues
    futex_lock(&statsLock);
    maybeUpdateQueuesLocked();

    // move toward x direction
    while (curCoord.x != dstCoord.x) {
        int step = curCoord.x < dstCoord.x ? 1 : -1;
        totalQWait += accountHopLocked(curCoord.x, curCoord.y, curCoord.x + step, curCoord.y, flitNum);
        totalHopLat += hopX;

        curCoord.x += step;
    }

    while (curCoord.y != dstCoord.y) {
        int step = curCoord.y < dstCoord.y ? 1 : -1;
        totalQWait += accountHopLocked(curCoord.x, curCoord.y, curCoord.x, curCoord.y + step, flitNum);
        totalHopLat += hopY;

        curCoord.y += step;
    }
    futex_unlock(&statsLock);

    return 2
        * (static_cast<uint64_t>(totalQWait) /* total wait-time */
            + totalHopLat                    /* send and receive */
            + (doesTurn ? 1 : 0)             /* horizontal stop to vertical and vice versa */
            + 2 * (flitNum - 1)); /* wormhole-like transmission delay | multiply by 2 because data is sent each 2 clk */
}

void MeshMD1Network::maybeUpdateQueuesLocked() {
    // not thread safe
    uint64_t curPhase = zinfo->numPhases;
    if (curPhase == lastQueueUpdatePhase) return;

    uint64_t deltaPhases = curPhase - lastQueueUpdatePhase;
    auto windowCycles = deltaPhases * zinfo->phaseLength;
    lastQueueUpdatePhase = curPhase;

    if (windowCycles == 0) return;

    constexpr double alpha = 0.3;
    constexpr double maxUtilization = 0.995;
    constexpr double maxQueueCycles = 1e6;

    // update queue delay
    for (auto& entry : linkStates) {
        LinkState& state = entry.second;
        double arrivalRate = static_cast<double>(state.flitsThisWindow) / windowCycles;
        double serviceRate = 1 / state.serviceTime; // 1 flit per cycle
        double rho = std::min(arrivalRate / serviceRate, maxUtilization);

        // moving avg on queue delay
        double newDelay = rho / ((2.0 * serviceRate) * (1 - rho));
        state.queueDelay = (1.0 - alpha) * state.queueDelay + alpha * newDelay;
        if (state.queueDelay > maxQueueCycles) state.queueDelay = maxQueueCycles;
    }

    // reset windows
    for (auto& entry : linkStates) {
        LinkState& state = entry.second;
        state.flitsThisWindow = 0;
        state.msgsThisWindow = 0;
    }
}

double MeshMD1Network::accountHopLocked(int x1, int y1, int x2, int y2, uint32_t flits) {
    // not thread safe
    if (x1 == x2 && y1 == y2) return 0.0;

    LinkId id{ x1, y1, x2, y2 };
    auto it = linkStates.find(id);

    if (it == linkStates.end()) {
        LinkState state;
        it = linkStates.insert(std::make_pair(id, state)).first;
    }

    LinkState& state = it->second;
    state.flitsThisWindow += flits;
    state.msgsThisWindow += 1;

    return state.queueDelay;
}

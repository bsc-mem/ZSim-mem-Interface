#include <fstream>
#include <string>

#include "log.h"
#include "network_fix_delay.h"

using std::ifstream;
using std::string;

FixedDelayNetwork::FixedDelayNetwork(std::string config) {
    ifstream inFile(config);

    if (!inFile) {
        panic("Could not open network description file %s", config.c_str());
    }

    while (inFile.good()) {
        string src, dst;
        uint32_t delay;
        inFile >> src;
        inFile >> dst;
        inFile >> delay;

        if (inFile.eof()) break;

        g_string s1 = g_string(src) + " " + g_string(dst);
        g_string s2 = g_string(dst) + " " + g_string(src);

        assert((delayMap.find(s1) == delayMap.end()));
        assert((delayMap.find(s2) == delayMap.end()));

        delayMap[s1] = delay;
        delayMap[s2] = delay;

        // info("Parsed %s %s %d", src.c_str(), dst.c_str(), delay);
    }

    inFile.close();
}

uint32_t FixedDelayNetwork::getRTT(const char* src, const char* dst, const bool isInvalidateMsg) {
    string key(src);
    key += " ";
    key += dst;
    auto key_c = key;
    g_string k = g_string(key);

    if (delayMap.find(k) != delayMap.end()) {
        return 2*delayMap[k];
    } else {
        warn("%s and %s have no entry in network description file, returning 0 latency", src, dst);
        return 0;
    }
}
#ifndef NETWORK_FIX_LAT_H_
#define NETWORK_FIX_LAT_H_

#include <string>
#include "network.h"
#include "memory_hierarchy.h"
#include "g_std/g_multimap.h"
#include "g_std/g_string.h"

class FixedDelayNetwork : public Network {
    public:
        FixedDelayNetwork(std::string config);
        uint32_t getRTT(const char* src, const char* dst, const bool isInvalidateMsg) override;
    
    private:
        g_map<g_string, uint32_t> delayMap;
};


#endif // NETWORK_FIX_LAT_H_
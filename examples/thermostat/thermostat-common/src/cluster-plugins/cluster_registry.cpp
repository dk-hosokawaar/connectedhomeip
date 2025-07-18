#include "cluster_registry.h"

using Factory = std::function<std::unique_ptr<IClusterHandler>()>;

static std::unordered_map<chip::ClusterId, Factory> gFactory;

std::unordered_map<chip::ClusterId, Factory>& ClusterFactoryMap() { return gFactory; }

IClusterHandler* GetHandler(chip::ClusterId cid)
{
    auto it = gFactory.find(cid);
    return (it == gFactory.end()) ? nullptr : it->second().release();
}

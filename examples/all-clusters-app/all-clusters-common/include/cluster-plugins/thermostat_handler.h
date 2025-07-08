#pragma once
#include "cluster_handler.h"

class ThermostatHandler final : public IClusterHandler {
public:
    void OnLocalAttributeChange(const chip::app::ConcreteAttributePath&,
                                chip::TLV::TLVReader*) override;
    void OnRemoteAttribute(const chip::app::ConcreteAttributePath&,
                           chip::TLV::TLVReader*) override {}
    void SyncToRemote(const EmberBindingTableEntry&,
                      chip::OperationalDeviceProxy*) override {};
};

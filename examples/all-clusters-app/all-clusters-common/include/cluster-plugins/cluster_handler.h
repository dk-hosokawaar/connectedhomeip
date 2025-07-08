#pragma once
#include <app/ConcreteAttributePath.h>
#include <app/util/binding-table.h>
#include <platform/CHIPDeviceLayer.h>
#include <lib/core/DataModelTypes.h>


namespace chip { class OperationalDeviceProxy; }

class IClusterHandler {
public:
    virtual ~IClusterHandler() = default;
    virtual void OnLocalAttributeChange(const chip::app::ConcreteAttributePath&,
                                        chip::TLV::TLVReader*) = 0;
    virtual void OnRemoteAttribute(const chip::app::ConcreteAttributePath&,
                                   chip::TLV::TLVReader*)     = 0;
    virtual void SyncToRemote(const EmberBindingTableEntry&,
                              chip::OperationalDeviceProxy*) = 0;
};

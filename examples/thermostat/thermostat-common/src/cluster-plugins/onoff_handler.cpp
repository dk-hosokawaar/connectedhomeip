#include "onoff_handler.h"
#include "cluster_registry.h"
#include <controller/InvokeInteraction.h>
#include <app/clusters/bindings/BindingManager.h> 


using namespace chip;
using namespace chip::app;

REGISTER_CLUSTER(Clusters::OnOff::Id, OnOffHandler);

void OnOffHandler::OnLocalAttributeChange(const ConcreteAttributePath& p,
                                          TLV::TLVReader* r)
{
    bool v = false;
    VerifyOrReturn(r->Get(v) == CHIP_NO_ERROR);
    BindingManager::GetInstance().NotifyBoundClusterChanged(
        p.mEndpointId, p.mClusterId, nullptr);
}

void OnOffHandler::SyncToRemote(const EmberBindingTableEntry& e,
                                OperationalDeviceProxy* d)
{
    Clusters::OnOff::Commands::Toggle::Type cmd;
    auto ok = [](const ConcreteCommandPath&, const StatusIB&, const auto&) {};
    auto er = [](CHIP_ERROR err){ ChipLogError(NotSpecified,"InvokeErr:%s",ErrorStr(err)); };
    Controller::InvokeCommandRequest(d->GetExchangeManager(),
                                     d->GetSecureSession().Value(),
                                     e.remote, cmd, ok, er);
}

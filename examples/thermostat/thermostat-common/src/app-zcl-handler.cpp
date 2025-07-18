#include "cluster-plugins/cluster_registry.h"
#include <app/clusters/bindings/BindingManager.h>

using namespace chip;
using namespace chip::app;

void MatterPostAttributeChangeCallback(
    const chip::app::ConcreteAttributePath & path,
    uint8_t /*type*/, uint16_t /*size*/, uint8_t * value)
{
    // if (auto *h = GetHandler(path.mClusterId))
    // {
    //     chip::TLV::TLVReader tmp;
    //     ChipLogError(NotSpecified, "新宝島")
    //     h->OnLocalAttributeChange(path, &tmp);
    // }
    // else
    // {
        ChipLogError(NotSpecified, "ミュージック")
        BindingManager::GetInstance()
            .NotifyBoundClusterChanged(path.mEndpointId, path.mClusterId, nullptr);
    // }
}

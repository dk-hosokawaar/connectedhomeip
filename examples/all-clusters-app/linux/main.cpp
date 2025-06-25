#include "AppMain.h"
#include "AppOptions.h"
#include "binding-handler.h"
#include "LightingStateReader.h"

#include <controller/CHIPDeviceController.h>
#include <platform/PlatformManager.h>

namespace chip { namespace Controller { class DeviceCommissioner; } }
extern chip::Controller::DeviceCommissioner * GetDeviceCommissioner();

// Network commissioning
namespace {
constexpr chip::EndpointId kNetworkCommissioningEndpointSecondary = 0xFFFE;

// ─── lighting-app の NodeId と Endpoint ───
constexpr chip::NodeId     kLightingNodeId   = 0x5678; // chip-tool で確認
constexpr chip::EndpointId kLightingEndpoint = 1;                        // lighting-app の既定は 1
} // anonymous namespace

// ─ network commissioning 省略 ─

static LightingStateReader gReader;

// ==== 新しいコールバックのシグネチャ ====
//   typedef void (*OnDeviceConnected)(void *, Messaging::ExchangeManager &, const SessionHandle &);
static void OnDeviceConnected(void *, chip::Messaging::ExchangeManager & exchangeMgr,
                              const chip::SessionHandle & session)
{
    // ReadPrepareParams は SessionHandle だけあれば生成可
    chip::app::ReadPrepareParams params(session);
    chip::app::AttributePathParams path;
    path.mEndpointId  = kLightingEndpoint;
    path.mClusterId   = chip::app::Clusters::OnOff::Id;
    path.mAttributeId = chip::app::Clusters::OnOff::Attributes::OnOff::Id;
    params.mpAttributePathParamsList     = &path;
    params.mAttributePathParamsListSize  = 1;

    gReader.SendRequest(exchangeMgr, params);   // LightingStateReader にラッパ追加済みとする
}

//   typedef void (*OnDeviceConnectionFailure)(void *, const ScopedNodeId &, CHIP_ERROR);
static void OnDeviceConnectFail(void *, const chip::ScopedNodeId & peerId, CHIP_ERROR err)
{
    ChipLogError(NotSpecified, "Connect failed: %s (peer 0x%016" PRIX64 ")",
                 chip::ErrorStr(err), static_cast<uint64_t>(peerId.GetNodeId()));
}

// ==== ラッパオブジェクト ====
//   第2引数は任意の context（今回は nullptr）
static chip::Callback::Callback<chip::OnDeviceConnected>       gConnCb(OnDeviceConnected, nullptr);
static chip::Callback::Callback<chip::OnDeviceConnectionFailure> gFailCb(OnDeviceConnectFail, nullptr);

static void TriggerRead(intptr_t)
{
    auto * commissioner = GetDeviceCommissioner();
    commissioner->GetConnectedDevice(kLightingNodeId, &gConnCb, &gFailCb);
}

int main(int argc, char * argv[])
{
    VerifyOrDie(ChipLinuxAppInit(argc, argv, AppOptions::GetOptions(),
                                 chip::MakeOptional(kNetworkCommissioningEndpointSecondary)) == 0);
    VerifyOrDie(InitBindingHandlers() == CHIP_NO_ERROR);

    chip::DeviceLayer::PlatformMgr().ScheduleWork(&TriggerRead, 0);
    ChipLinuxAppMainLoop();
    return 0;
}

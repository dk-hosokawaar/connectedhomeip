/*
 *  binding-handler.cpp – Device‑to‑Device Binding support (On/Off sample)
 *
 *  ▸ 起動時に BindingTable を走査して各 Binding を 1 回 Notify
 *  ▸ Notify → PendingNotificationMap に積み、CASE 接続完了時に
 *    HandleBoundDeviceChanged() が呼ばれて Peer の On/Off を読みに行く
 *  ▸ local EP‑1 の OnOff 変更で再び Notify し、Peer の状態と同期
 *
 *  Copyright (c) 2021‑2025 Project CHIP Authors
 *  Licensed under the Apache License 2.0
 */

#include "binding-handler.h"

/* ──────────────────────── Matter SDK ヘッダ ─────────────────────── */
#include <app-common/zap-generated/ids/Clusters.h>
#include <app-common/zap-generated/ids/Commands.h>
#include <app/CommandSender.h>
#include <app/InteractionModelEngine.h>
#include <app/ReadClient.h>
#include <app/clusters/bindings/BindingManager.h>
#include <app/server/Server.h>
#include <controller/InvokeInteraction.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/Span.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>
#include <app/util/binding-table.h>

#if defined(ENABLE_CHIP_SHELL)
#include <lib/shell/Engine.h>
#endif

using namespace chip;
using namespace chip::app;

/* ──────────────────────── Globals ─────────────────────── */
static bool sSwitchOnOffState = false; // 現地スイッチの論理状態

/* ====================================================================
 *  Utility: KickAllBindings() – 起動時に Pending を登録
 * ==================================================================== */
namespace {
void KickAllBindings()
{
    constexpr ClusterId kOnOff = Clusters::OnOff::Id;

    for (const EmberBindingTableEntry & e : BindingTable::GetInstance())
    {
        if (e.type != MATTER_UNICAST_BINDING)
            continue;
        if (e.clusterId.value_or(kOnOff) != kOnOff)
            continue;

        BindingManager::GetInstance().NotifyBoundClusterChanged(e.local, kOnOff, nullptr);
    }
}
} // namespace

/* ====================================================================
 *  Peer の On/Off 属性を 1 回 Read するヘルパ
 * ==================================================================== */
void ReadPeerOnOff(const EmberBindingTableEntry & entry, OperationalDeviceProxy & dev)
{
    class ReadCb : public ReadClient::Callback
    {
        void OnAttributeData(const ConcreteDataAttributePath & path, TLV::TLVReader * reader,
                             const StatusIB & status) override
        {
            bool val = false;
            if (status.mStatus == Protocols::InteractionModel::Status::Success && reader->Get(val) == CHIP_NO_ERROR)
            {
                ChipLogProgress(NotSpecified, "Peer OnOff = %d", val);
            }
        }
        void OnDone(ReadClient * rc) override
        {
            InteractionModelEngine::GetInstance()->RemoveReadClient(rc);
        }
    };
    static ReadCb sCb;

    ReadClient * rc = Platform::New<ReadClient>(InteractionModelEngine::GetInstance(),
                                                dev.GetExchangeManager(), sCb,
                                                ReadClient::InteractionType::Read);
    VerifyOrReturn(rc != nullptr, ChipLogError(NotSpecified, "ReadClient OOM"));
    InteractionModelEngine::GetInstance()->AddReadClient(rc);

    ReadPrepareParams params(dev.GetSecureSession().Value());
    AttributePathParams path{ entry.remote, Clusters::OnOff::Id, Clusters::OnOff::Attributes::OnOff::Id };
    params.mpAttributePathParamsList    = &path;
    params.mAttributePathParamsListSize = 1;

    CHIP_ERROR err = rc->SendRequest(params);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "SendRequest failed: %s", ErrorStr(err));
    }
}

/* ====================================================================
 *  Handlers registered to BindingManager
 * ==================================================================== */
static void HandleBoundDeviceChanged(const EmberBindingTableEntry & binding,
                                     OperationalDeviceProxy *       peerDev,
                                     void * /*context*/)
{
    if (binding.type != MATTER_UNICAST_BINDING)
        return;
    if (binding.local != 1 || binding.clusterId.value_or(Clusters::OnOff::Id) != Clusters::OnOff::Id)
        return;
    VerifyOrReturn(peerDev && peerDev->ConnectionReady(), ChipLogError(NotSpecified, "Peer not ready"));

    /* 1) Read 相手 OnOff */
    ReadPeerOnOff(binding, *peerDev);

    /* 2) ローカル状態に合わせてコマンド発行（任意） */
    auto ok  = [](const ConcreteCommandPath &, const StatusIB &, const auto &) {};
    auto err = [](CHIP_ERROR e) { ChipLogError(NotSpecified, "Invoke NG: %s", e.AsString()); };

    if (sSwitchOnOffState)
    {
        Clusters::OnOff::Commands::On::Type cmd;
        Controller::InvokeCommandRequest(peerDev->GetExchangeManager(), peerDev->GetSecureSession().Value(),
                                         binding.remote, cmd, ok, err);
    }
    else
    {
        Clusters::OnOff::Commands::Off::Type cmd;
        Controller::InvokeCommandRequest(peerDev->GetExchangeManager(), peerDev->GetSecureSession().Value(),
                                         binding.remote, cmd, ok, err);
    }
}

static void HandleContextRelease(void *) {}

/* ====================================================================
 *  Init – BindingManager セットアップ
 * ==================================================================== */
static void InitBindingHandlerInternal(intptr_t)
{
    auto & srv = Server::GetInstance();
    BindingManager::GetInstance().Init({ &srv.GetFabricTable(), srv.GetCASESessionManager(), &srv.GetPersistentStorage() });

    BindingManager::GetInstance().RegisterBoundDeviceChangedHandler(HandleBoundDeviceChanged);
    BindingManager::GetInstance().RegisterBoundDeviceContextReleaseHandler(HandleContextRelease);

    KickAllBindings();
}

CHIP_ERROR InitBindingHandlers()
{
    DeviceLayer::PlatformMgr().ScheduleWork(InitBindingHandlerInternal);

#if defined(ENABLE_CHIP_SHELL)
    using namespace Shell;
    const shell_command_t cmd = { [](int argc, char ** argv) -> CHIP_ERROR {
                                     if (argc == 1 && strcmp(argv[0], "on") == 0)
                                         sSwitchOnOffState = true;
                                     else if (argc == 1 && strcmp(argv[0], "off") == 0)
                                         sSwitchOnOffState = false;
                                     else
                                     {
                                         streamer_printf(streamer_get(), "Usage: switch [on|off]\n");
                                         return CHIP_NO_ERROR;
                                     }
                                     BindingManager::GetInstance().NotifyBoundClusterChanged(1, Clusters::OnOff::Id, nullptr);
                                     return CHIP_NO_ERROR;
                                 },
                                 "switch", "switch [on|off]" };
    Engine::Root().RegisterCommands(&cmd, 1);
#endif
    return CHIP_NO_ERROR;
}

/* ====================================================================
 *  Attribute write callback → NotifyBoundClusterChanged()
 * ==================================================================== */
void SwitchOnOffAttributeUpdated(EndpointId endpoint, bool value)
{
    sSwitchOnOffState = value;
    BindingManager::GetInstance().NotifyBoundClusterChanged(endpoint, Clusters::OnOff::Id, nullptr);
}

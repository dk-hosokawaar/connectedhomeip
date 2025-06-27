/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "binding-handler.h"

#include <app-common/zap-generated/ids/Clusters.h>
#include <app-common/zap-generated/ids/Commands.h>
#include <app/CommandSender.h>
#include <app/clusters/bindings/BindingManager.h>
#include <app/server/Server.h>
#include <controller/InvokeInteraction.h>
#include <lib/core/CHIPError.h>
#include <platform/CHIPDeviceLayer.h>

#include <app/ReadClient.h>
#include <app/InteractionModelEngine.h>
#include <lib/support/Span.h>
#include <platform/PlatformManager.h>
#include <lib/support/CHIPMem.h> 

#if defined(ENABLE_CHIP_SHELL)
#include <lib/shell/Engine.h> // nogncheck

using chip::Shell::Engine;
using chip::Shell::shell_command_t;
using chip::Shell::streamer_get;
using chip::Shell::streamer_printf;
#endif // defined(ENABLE_CHIP_SHELL)

static bool sSwitchOnOffState = false;
#if defined(ENABLE_CHIP_SHELL)
static void ToggleSwitchOnOff(bool newState)
{
    sSwitchOnOffState = newState;
    chip::BindingManager::GetInstance().NotifyBoundClusterChanged(1, chip::app::Clusters::OnOff::Id, nullptr);
}

static CHIP_ERROR SwitchCommandHandler(int argc, char ** argv)
{
    if (argc == 1 && strcmp(argv[0], "on") == 0)
    {
        ToggleSwitchOnOff(true);
        return CHIP_NO_ERROR;
    }
    if (argc == 1 && strcmp(argv[0], "off") == 0)
    {
        ToggleSwitchOnOff(false);
        return CHIP_NO_ERROR;
    }
    streamer_printf(streamer_get(), "Usage: switch [on|off]");
    return CHIP_NO_ERROR;
}

static void RegisterSwitchCommands()
{
    static const shell_command_t sSwitchCommand = { SwitchCommandHandler, "switch", "Switch commands. Usage: switch [on|off]" };
    Engine::Root().RegisterCommands(&sSwitchCommand, 1);
    return;
}
#endif // defined(ENABLE_CHIP_SHELL)

// static void BoundDeviceChangedHandler(const EmberBindingTableEntry & binding, chip::OperationalDeviceProxy * peer_device,
//                                       void * context)
// {
//     using namespace chip;
//     using namespace chip::app;

//     if (binding.type == MATTER_MULTICAST_BINDING)
//     {
//         ChipLogError(NotSpecified, "Group binding is not supported now");
//         return;
//     }

//     if (binding.type == MATTER_UNICAST_BINDING && binding.local == 1 &&
//         binding.clusterId.value_or(Clusters::OnOff::Id) == Clusters::OnOff::Id)
//     {
//         auto onSuccess = [](const ConcreteCommandPath & commandPath, const StatusIB & status, const auto & dataResponse) {
//             ChipLogProgress(NotSpecified, "OnOff command succeeds");
//         };
//         auto onFailure = [](CHIP_ERROR error) {
//             ChipLogError(NotSpecified, "OnOff command failed: %" CHIP_ERROR_FORMAT, error.Format());
//         };

//         VerifyOrDie(peer_device != nullptr && peer_device->ConnectionReady());
//         if (sSwitchOnOffState)
//         {
//             Clusters::OnOff::Commands::On::Type onCommand;
//             Controller::InvokeCommandRequest(peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value(),
//                                              binding.remote, onCommand, onSuccess, onFailure);
//         }
//         else
//         {
//             Clusters::OnOff::Commands::Off::Type offCommand;
//             Controller::InvokeCommandRequest(peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value(),
//                                              binding.remote, offCommand, onSuccess, onFailure);
//         }
//     }
// }

// ======================================================================
// Binding の接続確立 or 属性更新 通知時に呼ばれるハンドラ
// ======================================================================
static void BoundDeviceChangedHandler(const EmberBindingTableEntry & binding,
                                      chip::OperationalDeviceProxy * peerDev, void * context)
{
    using namespace chip;
    using namespace chip::app;

    // ── 1) multicast は今回は扱わない ────────────────────────────────
    if (binding.type == MATTER_MULTICAST_BINDING)
    {
        ChipLogError(NotSpecified, "Group binding is not supported");
        return;
    }

    // ── 2) OnOff クラスタ & local EP1 だけを対象にする ───────────────
    if (binding.type != MATTER_UNICAST_BINDING ||
        binding.local != 1 ||
        binding.clusterId.value_or(Clusters::OnOff::Id) != Clusters::OnOff::Id)
    {
        return;
    }

    // ── 3) CASE セッションが張れていなければ何もしない ─────────────
    VerifyOrReturn(peerDev != nullptr && peerDev->ConnectionReady(),
                   ChipLogError(NotSpecified, "Peer NOT ready"));

    // ── 4) まず能動的に相手の OnOff を読む ─────────────────────────
    ChipLogError(NotSpecified, "うんちっち学園三条")
    ReadPeerOnOff(binding, *peerDev);
    ChipLogError(NotSpecified, "しゃみなみ")

    // ── 5) （任意）自分のスイッチ状態に合わせてコマンドを発行 ────
    auto onOk  = [](const ConcreteCommandPath &, const StatusIB &, const auto &) {};
    auto onErr = [](CHIP_ERROR e) { ChipLogError(NotSpecified, "Cmd NG: %" CHIP_ERROR_FORMAT, e.Format()); };

    if (sSwitchOnOffState)
    {
        Clusters::OnOff::Commands::On::Type cmd;
        Controller::InvokeCommandRequest(peerDev->GetExchangeManager(),
                                         peerDev->GetSecureSession().Value(),
                                         binding.remote /*相手EP*/,
                                         cmd, onOk, onErr);
    }
    else
    {
        Clusters::OnOff::Commands::Off::Type cmd;
        Controller::InvokeCommandRequest(peerDev->GetExchangeManager(),
                                         peerDev->GetSecureSession().Value(),
                                         binding.remote,
                                         cmd, onOk, onErr);
    }
}


static void BoundDeviceContextReleaseHandler(void * context)
{
    (void) context;
}

static void InitBindingHandlerInternal(intptr_t arg)
{
    auto & server = chip::Server::GetInstance();
    chip::BindingManager::GetInstance().Init(
        { &server.GetFabricTable(), server.GetCASESessionManager(), &server.GetPersistentStorage() });
    chip::BindingManager::GetInstance().RegisterBoundDeviceChangedHandler(BoundDeviceChangedHandler);
    chip::BindingManager::GetInstance().RegisterBoundDeviceContextReleaseHandler(BoundDeviceContextReleaseHandler);
}

CHIP_ERROR InitBindingHandlers()
{
    // The initialization of binding manager will try establishing connection with unicast peers
    // so it requires the Server instance to be correctly initialized. Post the init function to
    // the event queue so that everything is ready when initialization is conducted.
    // TODO: Fix initialization order issue in Matter server.
    chip::DeviceLayer::PlatformMgr().ScheduleWork(InitBindingHandlerInternal);
#if defined(ENABLE_CHIP_SHELL)
    RegisterSwitchCommands();
#endif
    return CHIP_NO_ERROR;
}


void SwitchOnOffAttributeUpdated(chip::EndpointId endpoint, bool value)
{
    sSwitchOnOffState = value;           // ❶ 値を覚える
    // ❷ ep のクラスタ変更をバインディング経由で相手へ通知
    chip::BindingManager::GetInstance()
        .NotifyBoundClusterChanged(endpoint, chip::app::Clusters::OnOff::Id, nullptr);
}


// ─────────────────────────────────────────────────────────────
// Peer の OnOff 属性を読み取り、結果をログ出力するヘルパ
// ─────────────────────────────────────────────────────────────
void ReadPeerOnOff(const EmberBindingTableEntry & entry,
                   chip::OperationalDeviceProxy & dev)
{
    using namespace chip;
    using namespace chip::app;
    ChipLogProgress(NotSpecified, "このまま君をつれていくと")

    class TmpReadCallback : public ReadClient::Callback
    {
        void OnAttributeData(const ConcreteDataAttributePath & path,
                            TLV::TLVReader * reader,
                            const StatusIB & status) override
        {
            bool onoff = false;
            if (status.mStatus == Protocols::InteractionModel::Status::Success &&
                reader->Get(onoff) == CHIP_NO_ERROR)
            {
                // ChipLogProgress(NotSpecified, "★ Peer OnOff = %d", onoff);
                ChipLogProgress(NotSpecified, "★ か０っかっかっかつおぶし = %d", onoff);
            }
        }
        void OnDone(ReadClient * rc) override
        {
            chip::app::InteractionModelEngine::GetInstance()->RemoveReadClient(rc);
        }
    };
    static TmpReadCallback sCb;

    ReadClient * rc = chip::Platform::New<ReadClient>(
            chip::app::InteractionModelEngine::GetInstance(),
            dev.GetExchangeManager(),
            sCb,                                       // ← 参照渡し
            ReadClient::InteractionType::Read);

    chip::app::InteractionModelEngine::GetInstance()->AddReadClient(rc);


    // 2) リクエストを組み立てて送信
    ReadPrepareParams params(dev.GetSecureSession().Value());
    AttributePathParams path{ entry.remote,                                     // 相手 EP
                              Clusters::OnOff::Id,
                              Clusters::OnOff::Attributes::OnOff::Id };
    params.mpAttributePathParamsList    = &path;
    params.mAttributePathParamsListSize = 1;

    CHIP_ERROR err = rc->SendRequest(params);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "SendRequest failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

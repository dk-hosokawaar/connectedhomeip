/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    All rights reserved.
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

#include "AppMain.h"
#include <app-common/zap-generated/callback.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/CommandHandler.h>
#include <app/clusters/identify-server/identify-server.h>
#include <app/clusters/thermostat-server/thermostat-server.h>
#include "binding-handler.h"
#include <lib/shell/Engine.h>

#include "thermostat-delegate-impl.h"

#if defined(ENABLE_CHIP_SHELL)
#include <lib/support/CHIPMem.h> // （任意：ログ用途）

using chip::Shell::Engine;

namespace {

// コマンド: "hello" → 単純な挨拶を表示
static CHIP_ERROR CmdHello(int argc, char * argv[])
{
    ChipLogProgress(NotSpecified, "Hello from custom shell command!");
    return CHIP_NO_ERROR;
}

// コマンド: "temp" → 外部温度 (例) を表示
// 実装は必要に応じて変えてください。ここでは引数無しで固定値を表示。
static CHIP_ERROR CmdTemp(int argc, char * argv[])
{
    // TODO: 実際の温度取得ロジックに置き換え
    ChipLogProgress(NotSpecified, "External temperature: 23.5C (demo)");
    return CHIP_NO_ERROR;
}

// Shell が保持するコマンドテーブルに渡す記述子配列。
// ※ static const (静的記憶域期間) にすることで、登録後も寿命切れしない。
static const chip::Shell::shell_command_t sCustomCommands[] = {
    { &CmdHello, "hello", "Print greeting string" },
    { &CmdTemp,  "temp",  "Show demo external temperature" },
};

void RegisterCustomShellCommands()
{
    Engine::Root().RegisterCommands(sCustomCommands, ArraySize(sCustomCommands));
}

} // namespace
#endif // ENABLE_CHIP_SHELL

using namespace chip;
using namespace chip::app;
// using namespace chip::app::Clusters;

void OnIdentifyStart(Identify *)
{
    ChipLogProgress(Zcl, "OnIdentifyStart");
}

void OnIdentifyStop(Identify *)
{
    ChipLogProgress(Zcl, "OnIdentifyStop");
}

void OnTriggerEffect(Identify * identify)
{
    switch (identify->mCurrentEffectIdentifier)
    {
    case Clusters::Identify::EffectIdentifierEnum::kBlink:
        ChipLogProgress(Zcl, "Clusters::Identify::EffectIdentifierEnum::kBlink");
        break;
    case Clusters::Identify::EffectIdentifierEnum::kBreathe:
        ChipLogProgress(Zcl, "Clusters::Identify::EffectIdentifierEnum::kBreathe");
        break;
    case Clusters::Identify::EffectIdentifierEnum::kOkay:
        ChipLogProgress(Zcl, "Clusters::Identify::EffectIdentifierEnum::kOkay");
        break;
    case Clusters::Identify::EffectIdentifierEnum::kChannelChange:
        ChipLogProgress(Zcl, "Clusters::Identify::EffectIdentifierEnum::kChannelChange");
        break;
    default:
        ChipLogProgress(Zcl, "No identifier effect");
        return;
    }
}

static Identify gIdentify0 = {
    chip::EndpointId{ 0 }, OnIdentifyStart, OnIdentifyStop, Clusters::Identify::IdentifyTypeEnum::kVisibleIndicator,
    OnTriggerEffect,
};

static Identify gIdentify1 = {
    chip::EndpointId{ 1 }, OnIdentifyStart, OnIdentifyStop, Clusters::Identify::IdentifyTypeEnum::kVisibleIndicator,
    OnTriggerEffect,
};

void ApplicationInit() {}

void ApplicationShutdown() {}

int main(int argc, char * argv[])
{
    VerifyOrDie(ChipLinuxAppInit(argc, argv) == 0);
    VerifyOrDie(InitBindingHandlers() == CHIP_NO_ERROR);

    #if defined(ENABLE_CHIP_SHELL)
    // Shell エンジン初期化（ChipLinuxAppInit 内）完了後にコマンド登録。
    RegisterCustomShellCommands();
    #endif

    ChipLinuxAppMainLoop();
    return 0;
}

using namespace chip::app::Clusters::Thermostat;
void emberAfThermostatClusterInitCallback(EndpointId endpoint)
{
    // Register the delegate for the Thermostat
    auto & delegate = ThermostatDelegate::GetInstance();

    SetDefaultDelegate(endpoint, &delegate);
}

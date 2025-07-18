#include "thermostat_handler.h"
#include "cluster_registry.h"
#include "aircon_bridge.h"
#include <controller/InvokeInteraction.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <protocols/interaction_model/StatusCode.h>


using namespace chip;
using namespace chip::app;
using chip::Protocols::InteractionModel::Status;

static uint8_t  sLastMode     = 3;    // default COOL
static int16_t sLastSetpoint = 2300; // default 23.00 °C

// ★ ClusterId とクラスを紐付け
REGISTER_CLUSTER(Clusters::Thermostat::Id, ThermostatHandler);

void ThermostatHandler::OnLocalAttributeChange(const ConcreteAttributePath& p,
                                          TLV::TLVReader* r)
{
    ChipLogError(NotSpecified, "さよならはエモーション");
    // if (p.mAttributeId ==
    //         Clusters::Thermostat::Attributes::OccupiedCoolingSetpoint::Id &&
    //         r != nullptr)
    //     {
    //         ChipLogError(NotSpecified, "ナイトフィッシングイズグッド")
    //         int16_t setpoint = 0;
    //         VerifyOrReturn(r->Get(setpoint) == CHIP_NO_ERROR);
    //         // 例: 2300 → 23 °C
    //         aircon_bridge::SendSetpoint(setpoint);
    //     }
    if (p.mAttributeId ==
        Clusters::Thermostat::Attributes::OccupiedCoolingSetpoint::Id)
    {
        ChipLogError(NotSpecified, "ナイトフィッシングイズグッド");
        int16_t setpoint = 0;
        Status st = Clusters::Thermostat::Attributes::OccupiedCoolingSetpoint::Get(
                        p.mEndpointId, &setpoint);
        VerifyOrReturn(st == Status::Success);   
        ChipLogProgress(NotSpecified, "Call API with setpoint=%d", setpoint);
        aircon_bridge::SendControl(sLastMode, setpoint);
    }

    // ★ SystemMode が来たとき
    if (p.mAttributeId == Clusters::Thermostat::Attributes::SystemMode::Id)
    {
        Clusters::Thermostat::SystemModeEnum sysMode;
        Status st = Clusters::Thermostat::Attributes::SystemMode::Get(
                    p.mEndpointId, &sysMode);
        VerifyOrReturn(st == Status::Success);

        using SM = Clusters::Thermostat::SystemModeEnum;
        switch (sysMode) {
            case SM::kHeat:    sLastMode = 4; break;
            case SM::kFanOnly: sLastMode = 6; break;
            default:           sLastMode = 3; break;   // Cool 他
        }
        aircon_bridge::SendControl(sLastMode, sLastSetpoint);
    }   
}

// ────────────────────────────────────────
// ② Peer → Local を何も同期しないなら空で OK
// ────────────────────────────────────────
// void OnRemoteAttribute(const ConcreteAttributePath &,
//     TLV::TLVReader *) {}

// void SyncToRemote(const EmberBindingTableEntry &,
//     OperationalDeviceProxy *) {}
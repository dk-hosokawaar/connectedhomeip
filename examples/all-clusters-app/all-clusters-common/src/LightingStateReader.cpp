#include "LightingStateReader.h"
#include <app/util/attribute-storage.h>
#include <app-common/zap-generated/cluster-objects.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

CHIP_ERROR LightingStateReader::ReadOnOff(DeviceProxy * dev, EndpointId ep)
{
    // ── ①新API：SessionHandle だけを渡すコンストラクタ ──
    ReadPrepareParams params(dev->GetSecureSession().Value());

    // ── ②もし↑でも合わない場合（さらに新しい SDK）────
    // ReadPrepareParams params;
    // params.mSessionHolder.SetSession(dev->GetSecureSession().Value());
    AttributePathParams path;
    path.mEndpointId  = ep;
    path.mClusterId   = OnOff::Id;
    path.mAttributeId = OnOff::Attributes::OnOff::Id;
    params.mpAttributePathParamsList = &path;
    params.mAttributePathParamsListSize = 1;
    return mReadClient.SendRequest(params);
}

void LightingStateReader::OnAttributeData(const ConcreteDataAttributePath & path,
                                          TLV::TLVReader * data, const StatusIB & status)
{
    bool onoff = false;
    if (data->Get(onoff) == CHIP_NO_ERROR)
    {
        ChipLogProgress(NotSpecified, "★ Lighting OnOff = %s", onoff ? "ON" : "OFF");
    }
}

//追加実装：ExchangeManager と ReadPrepareParams を受け取り mReadClient に転送
CHIP_ERROR LightingStateReader::SendRequest(chip::Messaging::ExchangeManager & exch,
                                            chip::app::ReadPrepareParams & params)
{
    // ReadClient は内部的に ExchangeManager を持っているので特別な設定は不要
    return mReadClient.SendRequest(params);
}

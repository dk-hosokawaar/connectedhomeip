#pragma once
#include <app/ReadClient.h>
#include <app/InteractionModelEngine.h>

class LightingStateReader : public chip::app::ReadClient::Callback
{
public:
    LightingStateReader() :
        mReadClient(
            chip::app::InteractionModelEngine::GetInstance(),                // IM Engine
            chip::app::InteractionModelEngine::GetInstance()->GetExchangeManager(), // ExchangeMgr
            *this,                                                           // Callback (=self)
            chip::app::ReadClient::InteractionType::Read)                    // Read interaction
    {}

    CHIP_ERROR ReadOnOff(chip::DeviceProxy * dev, chip::EndpointId ep);
    // ←★ ここに追加 ─ ExchangeManager と ReadPrepareParams を受け取る汎用メソッド
    CHIP_ERROR SendRequest(chip::Messaging::ExchangeManager & exch, chip::app::ReadPrepareParams & params);


    // ReadClient::Callback
    void OnAttributeData(const chip::app::ConcreteDataAttributePath & path,
                         chip::TLV::TLVReader * data, const chip::app::StatusIB & status) override;
    void OnDone(chip::app::ReadClient * client) override {}

private:
    chip::app::ReadClient mReadClient;
};

/*
 *  binding-handler.cpp – Device‑to‑Device Binding  (Subscribe variant / Matter 1.4)
 *  ───────────────────────────────────────────────────────────────────────────
 *  2025‑06‑30  汎用クラスタ対応版
 *
 *  概要
 *  ▸ BindingTable の clusterId をそのまま利用して、On/Off 固定を解消
 *  ▸ 任意クラスタの属性を Subscribe (Min 0 / Max 20 s, keep‑alive)
 *  ▸ 必要に応じてローカル→リモート同期コマンドを送出（クラスタ毎マッピング）
 *
 *  変更点（旧版 → 本版）
 *    • KickAllBindings() から Clusters::OnOff::Id のハードコード削除
 *    • SubscribePeerAttribute() を新設してワイルドカード購読をサポート
 *    • HandleBoundDeviceChanged() を汎用化
 *    • MatterPostAttributeChangeCallback() で clusterId を動的判定
 */

#include "binding-handler.h"

/* ──────────────── Matter SDK ─────────────── */
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/CommandSender.h>
#include <app/InteractionModelEngine.h>
#include <app/ReadClient.h>
#include <app/clusters/bindings/BindingManager.h>
#include <app/server/Server.h>
#include <controller/InvokeInteraction.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>
#include <app/util/binding-table.h>
#include <inttypes.h> // PRIx32 用
#include "cluster-plugins/cluster_registry.h"
#include "cluster-plugins/cluster_handler.h"
#include <app/WriteClient.h>


#if defined(ENABLE_CHIP_SHELL)
#include <lib/shell/Engine.h>
#endif

using namespace chip;
using namespace chip::app;

/* =======================================================================
 *  KickAllBindings() – Boot 時に BindingTable の全エントリへ Notify
 * =======================================================================*/
namespace {
void KickAllBindings()
{
    size_t idx = 0;
    
    for (const EmberBindingTableEntry & e : BindingTable::GetInstance())
    {
        /* ---- ① BindingTable の内容を出力 ---- */
        ChipLogError(NotSpecified,
                    "[Bind %zu] type=%u fabric=%u localEP=%u → "
                    "nodeId=0x" ChipLogFormatX64 " remoteEP=%u "
                    "cluster=0x%08" PRIx32, idx,
                    static_cast<unsigned>(e.type),
                    static_cast<unsigned>(e.fabricIndex),
                    static_cast<unsigned>(e.local),
                    ChipLogValueX64(e.nodeId),                   // 64-bit ログマクロ :contentReference[oaicite:5]{index=5}
                    static_cast<unsigned>(e.remote),
                    static_cast<uint32_t>(e.clusterId.value_or(0)));
        if (e.type == MATTER_UNICAST_BINDING)
        {
            ClusterId cid = e.clusterId.value_or(0 /* wildcard */);
            BindingManager::GetInstance().NotifyBoundClusterChanged(e.local, cid, nullptr);
        }
    }
}
} // namespace

/* =======================================================================
 *  SubscribePeerAttribute() – 任意クラスタの属性を購読
 * =======================================================================*/
static void SubscribePeerAttribute(const EmberBindingTableEntry & entry, OperationalDeviceProxy & dev, ClusterId targetCluster)
{

    class SubCb : public ReadClient::Callback
    {
        ReadClient * mClient = nullptr;

    public:
        void Attach(ReadClient * c) { mClient = c; }

        void OnAttributeData(const ConcreteDataAttributePath & path, TLV::TLVReader * r,
                             const StatusIB & status) override
        {
            if (status.mStatus != Protocols::InteractionModel::Status::Success || r == nullptr)
                return;

            if (auto* h = GetHandler(path.mClusterId)) {
                h->OnRemoteAttribute(path, r);
            }
        }
        void OnReportEnd() override {}
        void OnError(CHIP_ERROR e) override { ChipLogError(NotSpecified, "Subscribe error: %s", ErrorStr(e)); }
        void OnDone(ReadClient * rc) override { InteractionModelEngine::GetInstance()->RemoveReadClient(rc); }
    };

    static SubCb sCb; // 単一購読想定 (必要なら動的確保に変更)

    ReadClient * rc = Platform::New<ReadClient>(InteractionModelEngine::GetInstance(),
                                                dev.GetExchangeManager(), sCb,
                                                ReadClient::InteractionType::Subscribe);
    VerifyOrReturn(rc, ChipLogError(NotSpecified, "ReadClient OOM"));
    sCb.Attach(rc);

    ReadPrepareParams params(dev.GetSecureSession().Value());
    params.mMinIntervalFloorSeconds   = 0;
    params.mMaxIntervalCeilingSeconds = 30;
    params.mKeepSubscriptions         = true;

    /* cluster 全体を購読 (AttributeId ワイルドカード) */
    AttributePathParams path{ entry.remote, targetCluster, kInvalidAttributeId };
    params.mpAttributePathParamsList    = &path;
    params.mAttributePathParamsListSize = 1;

    CHIP_ERROR err = rc->SendAutoResubscribeRequest(std::move(params));
    if (err != CHIP_NO_ERROR)
        ChipLogError(NotSpecified, "Subscribe failed: %s", ErrorStr(err));
    
}


/* =======================================================================
 *  HandleBoundDeviceChanged() – CASE 接続後の初期処理
 * =======================================================================*/
static void HandleBoundDeviceChanged(const EmberBindingTableEntry & binding, OperationalDeviceProxy * peerDev, void *)
{
    if (binding.type != MATTER_UNICAST_BINDING || !peerDev || !peerDev->ConnectionReady()){
        ChipLogError(NotSpecified, "椎名林檎！！")
        return;
    }

    ClusterId cid = binding.clusterId.value_or(0 /* wildcard */);

    /* (1) 相手属性を購読 */
    SubscribePeerAttribute(binding, *peerDev, cid);

    /* (2) 必要に応じてローカル→リモートの同期コマンド */
    if (auto* h = GetHandler(cid))
        h->SyncToRemote(binding, peerDev);
}

static void HandleContextRelease(void *) {}

/* =======================================================================
 *  InitBindingHandlers() – BindingManager 初期化ラッパ
 * =======================================================================*/
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

// #if defined(ENABLE_CHIP_SHELL)
//     /* shell command: "switch on|off" (OnOff テスト用) */
//     using namespace Shell;
//     const shell_command_t cmd = {
//         [](int argc, char ** argv) -> CHIP_ERROR {
//             // if (argc == 1 && strcmp(argv[0], "on") == 0)
//             //     sLocalState.onOff = true;
//             // else if (argc == 1 && strcmp(argv[0], "off") == 0)
//             //     sLocalState.onOff = false;
//             // else
//             // {
//             //     streamer_printf(streamer_get(), "Usage: switch [on|off]\n");
//             //     return CHIP_NO_ERROR;
//             // }
//             BindingManager::GetInstance().NotifyBoundClusterChanged(1 /* EP‑1 */, Clusters::OnOff::Id, nullptr);
//             return CHIP_NO_ERROR;
//         },
//         "switch", "switch [on|off]" };
//     Engine::Root().RegisterCommands(&cmd, 1);
// #endif
    return CHIP_NO_ERROR;
}

/* =======================================================================
 *  MatterPostAttributeChangeCallback() – ローカル属性変更通知
 * =======================================================================*/
// void MatterPostAttributeChangeCallback(const ConcreteAttributePath & path, uint8_t /*type*/, uint16_t /*size*/, uint8_t * val)
// {

//     if (auto* h = GetHandler(path.mClusterId)) {
//         chip::TLV::TLVReader tmp;
//         // Reader をセットアップ（割愛）
//         h->OnLocalAttributeChange(path, &tmp);
//         ChipLogError(NotSpecified, "ちゅぽもてぃんぐ")
//     } else {
//         // ハンドラ無しクラスタだけ中央で通知
//         BindingManager::GetInstance().NotifyBoundClusterChanged(
//             path.mEndpointId, path.mClusterId, nullptr);
//         ChipLogError(NotSpecified, "ナインゴラン")
//     }

//     ChipLogError(NotSpecified, "わからん")
// }

// static CHIP_ERROR MirrorAttributeWrite(const EmberBindingTableEntry& entry,
//                                        chip::TLV::TLVReader* reader)
// {
//     using namespace chip;
//     using namespace chip::app;
//     Messaging::ExchangeManager* em = Server::GetInstance().GetExchangeManager();

//     // ❶ WriteClient の生成（レスポンス抑止）
//     WriteClient wc(em, nullptr /* callback */,
//                    chip::NullOptional /* timedWriteTimeoutMs */,
//                    /* suppressResponse = */ true);

//     // ❷ AttributeDataIBs をエンコード
//     AttributePathParams path{ entry.remote,
//                               entry.clusterId.ValueOr(kInvalidClusterId),
//                               kInvalidAttributeId };          // 全属性

//     AttributeDataIB dataIb;
//     dataIb.DataVersion.SetNull();          // DataVersion は省略
//     dataIb.Data = *reader;                 // 受信 TLV をそのままコピー

//     ReturnErrorOnFailure(wc.EncodeAttribute(path, dataIb));
//     ReturnErrorOnFailure(wc.Finish());     // パケット確定
//     return CHIP_NO_ERROR;                  // fire-and-forget
// }
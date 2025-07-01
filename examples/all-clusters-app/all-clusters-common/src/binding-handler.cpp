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

#if defined(ENABLE_CHIP_SHELL)
#include <lib/shell/Engine.h>
#endif

using namespace chip;
using namespace chip::app;

/* ────────────────────── ローカル状態 ──────────────────────
 *  クラスタごとの bool 状態を保持。ここではサンプルとして OnOff / LevelControl
 *  (100 = ON / 0 = OFF) だけだが、必要に応じて拡張する。
 */
namespace {
struct LocalState
{
    bool onOff                      = false; // Clusters::OnOff
    uint8_t level                   = 0;     // Clusters::LevelControl (0‑254)
    /* 追加クラスタ用フィールド … */
};
static LocalState sLocalState;
} // namespace

/* ──────────────────── コマンドマッピング ─────────────────── */
namespace {
struct SyncCommandEntry
{
    ClusterId clusterId;
    CommandId cmdOn;  // あるいは "有効化" を示すコマンド
    CommandId cmdOff; // あるいは "無効化" を示すコマンド
};

constexpr SyncCommandEntry kSyncCmdTable[] = {
    { Clusters::OnOff::Id,
      Clusters::OnOff::Commands::On::Id,
      Clusters::OnOff::Commands::Off::Id },

    /* LevelControl は MoveToLevel コマンド (254=ON, 0=OFF) を使う */
    { Clusters::LevelControl::Id,
      Clusters::LevelControl::Commands::MoveToLevel::Id,
      Clusters::LevelControl::Commands::MoveToLevel::Id },

    /* 例: ColorControl を Hue 0/120° に切り替える */
    { Clusters::ColorControl::Id,
      Clusters::ColorControl::Commands::MoveToHue::Id,
      Clusters::ColorControl::Commands::MoveToHue::Id }
};

static CommandId FindCmdOn(ClusterId cid)
{
    for (auto & e : kSyncCmdTable)
        if (e.clusterId == cid)
            return e.cmdOn;
    return kInvalidCommandId;
}

static CommandId FindCmdOff(ClusterId cid)
{
    for (auto & e : kSyncCmdTable)
        if (e.clusterId == cid)
            return e.cmdOff;
    return kInvalidCommandId;
}
} // namespace

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
// static void SubscribePeerAttribute(const EmberBindingTableEntry & entry, OperationalDeviceProxy & dev, ClusterId targetCluster)
static void SubscribePeerAttribute(const EmberBindingTableEntry & entry,
                                   OperationalDeviceProxy & dev)
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

            /* クラスタ毎に処理を振り分ける */
            switch (path.mClusterId)
            {
            case Clusters::OnOff::Id: {
                bool v = false;
                if (r->Get(v) == CHIP_NO_ERROR)
                {
                    ChipLogProgress(NotSpecified,
                        "[SUB] EP=%u Clus=0x%04" PRIx32 " Attr=0x%04" PRIx32 "  Peer OnOff = %d",
                        static_cast<unsigned>(path.mEndpointId),
                        static_cast<uint32_t>(path.mClusterId),
                        static_cast<uint32_t>(path.mAttributeId),
                        v);
                        sLocalState.onOff = v;
                }
                break;
            }
            case Clusters::LevelControl::Id: {
                uint8_t lvl = 0;
                if (r->Get(lvl) == CHIP_NO_ERROR)
                {
                    ChipLogProgress(NotSpecified,
                        "[SUB] EP=%u Clus=0x%04" PRIx32 " Attr=0x%04" PRIx32 "  Peer Level = %u",
                        static_cast<unsigned>(path.mEndpointId),
                        static_cast<uint32_t>(path.mClusterId),
                        static_cast<uint32_t>(path.mAttributeId),
                        lvl);
                    sLocalState.level = lvl;
                }
                break;
            }
            /* 他クラスタを追加 … */
            default:
                break;
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
    params.mMaxIntervalCeilingSeconds = 20;
    params.mKeepSubscriptions         = true;

    /* cluster 全体を購読 (AttributeId ワイルドカード) */
    // AttributePathParams path{ entry.remote, targetCluster, kInvalidAttributeId };
    /* ── クラスタもワイルドカードにして EP 全体を購読 ── */
    AttributePathParams path{ entry.remote,
                              kInvalidClusterId,
                              kInvalidAttributeId };
    params.mpAttributePathParamsList    = &path;
    params.mAttributePathParamsListSize = 1;

    CHIP_ERROR err = rc->SendAutoResubscribeRequest(std::move(params));
    if (err != CHIP_NO_ERROR)
        ChipLogError(NotSpecified, "Subscribe failed: %s", ErrorStr(err));
    
}

/* =======================================================================
 *  SendSyncedCommand() – ローカル状態に合わせてリモートへコマンド送信
 * =======================================================================*/
static void SendSyncedCommand(const EmberBindingTableEntry & binding, OperationalDeviceProxy * dev, ClusterId cid)
{
    CommandId cmd = kInvalidCommandId;

    if (cid == Clusters::OnOff::Id)
        cmd = sLocalState.onOff ? FindCmdOn(cid) : FindCmdOff(cid);
    else if (cid == Clusters::LevelControl::Id)
        cmd = sLocalState.level > 0 ? FindCmdOn(cid) : FindCmdOff(cid);
    /* 他クラスタ条件… */

    if (cmd == kInvalidCommandId)
        return; // 同期不要

    auto ok  = [](const ConcreteCommandPath &, const StatusIB &, const auto &) {};
    auto err = [](CHIP_ERROR e) { ChipLogError(NotSpecified, "Invoke NG: %s", ErrorStr(e)); };

    switch (cid)
    {
    case Clusters::OnOff::Id: {
        if (cmd == Clusters::OnOff::Commands::On::Id)
        {
            ChipLogError(NotSpecified, "ヘンダーソン")
            Clusters::OnOff::Commands::On::Type c;
            Controller::InvokeCommandRequest(dev->GetExchangeManager(), dev->GetSecureSession().Value(),
                                             binding.remote, c, ok, err);
        }
        else
        {
            ChipLogError(NotSpecified, "でヘア")
            Clusters::OnOff::Commands::Off::Type c;
            Controller::InvokeCommandRequest(dev->GetExchangeManager(), dev->GetSecureSession().Value(),
                                             binding.remote, c, ok, err);
        }
        break;
    }
    case Clusters::LevelControl::Id: {
        Clusters::LevelControl::Commands::MoveToLevel::Type c;
        c.level  = (cmd == FindCmdOn(cid)) ? static_cast<uint8_t>(254) : static_cast<uint8_t>(0);
        c.transitionTime = 0;
        Controller::InvokeCommandRequest(dev->GetExchangeManager(), dev->GetSecureSession().Value(),
                                         binding.remote, c, ok, err);
        break;
    }
    /* 他クラスタ送信用 case 追加 … */
    default:
        break;
    }

    ChipLogError(NotSpecified, "ディバラ")
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

    ChipLogError(NotSpecified, "ドラゴンボール！！")
    ClusterId cid = binding.clusterId.value_or(0 /* wildcard */);

    ChipLogError(NotSpecified, "ワンピース")
    /* (1) 相手属性を購読 */
    // SubscribePeerAttribute(binding, *peerDev, cid);
    SubscribePeerAttribute(binding, *peerDev);

    ChipLogError(NotSpecified, "ブルーノフェルナンデス")
    /* (2) 必要に応じてローカル→リモートの同期コマンド */
    SendSyncedCommand(binding, peerDev, cid);
    ChipLogError(NotSpecified, "マーカスラッシュフォード")
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

    ChipLogError(NotSpecified, "シュバインシュタイガー")
    KickAllBindings();
    ChipLogError(NotSpecified, "トーマスミュラー")
}

CHIP_ERROR InitBindingHandlers()
{
    DeviceLayer::PlatformMgr().ScheduleWork(InitBindingHandlerInternal);

#if defined(ENABLE_CHIP_SHELL)
    /* shell command: "switch on|off" (OnOff テスト用) */
    using namespace Shell;
    const shell_command_t cmd = {
        [](int argc, char ** argv) -> CHIP_ERROR {
            if (argc == 1 && strcmp(argv[0], "on") == 0)
                sLocalState.onOff = true;
            else if (argc == 1 && strcmp(argv[0], "off") == 0)
                sLocalState.onOff = false;
            else
            {
                streamer_printf(streamer_get(), "Usage: switch [on|off]\n");
                return CHIP_NO_ERROR;
            }
            BindingManager::GetInstance().NotifyBoundClusterChanged(1 /* EP‑1 */, Clusters::OnOff::Id, nullptr);
            return CHIP_NO_ERROR;
        },
        "switch", "switch [on|off]" };
    Engine::Root().RegisterCommands(&cmd, 1);
#endif
    return CHIP_NO_ERROR;
}

/* =======================================================================
 *  MatterPostAttributeChangeCallback() – ローカル属性変更通知
 * =======================================================================*/
void MatterPostAttributeChangeCallback(const ConcreteAttributePath & path, uint8_t /*type*/, uint16_t /*size*/, uint8_t * val)
{
    ChipLogError(NotSpecified, "助けて")
    bool needNotify = false;

    switch (path.mClusterId)
    {
    case Clusters::OnOff::Id: {
        ChipLogError(NotSpecified, "シャングリラ")
        bool newVal = (*val != 0);
        if (sLocalState.onOff != newVal)
        {
            sLocalState.onOff = newVal;
            needNotify        = true;
        }
        ChipLogError(NotSpecified, "ポンポンウェイウェイウェイ")
        break;
    }
    case Clusters::LevelControl::Id: {
        uint8_t newLvl = *val;
        if (sLocalState.level != newLvl)
        {
            sLocalState.level = newLvl;
            needNotify        = true;
        }
        break;
    }
    /* 他クラスタ追加 … */
    default:
        break;
    }

    if (needNotify)
    {
        BindingManager::GetInstance().NotifyBoundClusterChanged(path.mEndpointId, path.mClusterId, nullptr);
    }

    ChipLogError(NotSpecified, "わからん")
}

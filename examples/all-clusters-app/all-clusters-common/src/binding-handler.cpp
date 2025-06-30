/*
 *  binding-handler.cpp – Device-to-Device Binding  (Subscribe 版 / Matter 1.4)
 *  ───────────────────────────────────────────────────────────────────────────
 *  構成要約
 *    1. 起動直後         : KickAllBindings() で BindingTable を走査し、Notify 発火
 *    2. CASE セッション後 : HandleBoundDeviceChanged() 内で
 *         a) SubscribePeerOnOff()  … 相手の OnOff 属性を購読
 *         b) (任意)  ローカル状態同期コマンド ──★ 今はコメントアウトして無効
 *    3. Subscribe       : Min=0 / Max=20 秒。変化時は即、変化なしでも 20 秒で keep-alive
 *    4. 再接続          : SendAutoResubscribeRequest() + mKeepSubscriptions=true が自動対応
 *
 *  変更履歴メモ
 *    • Matter 1.4 に合わせ Callback シグネチャ修正 (OnReportEnd/OnError)
 *    • ReadPrepareParams から mAutoResubscribe → mKeepSubscriptions へ置換
 *    • ReadPeerOnOff() は削除し、SubscribePeerOnOff() を直接使用
 *    • 初期化時に Off コマンドを送らないよう同期ロジックをコメントアウト
 */

#include "binding-handler.h"

/* ──────────────── Matter SDK ─────────────── */
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/CommandSender.h>
#include <app/InteractionModelEngine.h>
#include <app/ReadClient.h>                       // 購読・読取を行うクライアント
#include <app/clusters/bindings/BindingManager.h> // Binding テーブル管理
#include <app/server/Server.h>                    // FabricTable / CASE SessionMgr 取得用
#include <controller/InvokeInteraction.h>         // InvokeCommandRequest()
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>
#include <app/util/binding-table.h>               // EmberBindingTableEntry

#if defined(ENABLE_CHIP_SHELL)
#include <lib/shell/Engine.h>
#endif

using namespace chip;
using namespace chip::app;

/* ───────────────────── Globals ──────────────────────
 *  モックスイッチ (EP-1) のローカル状態。Subscribe 後に
 *  相手の OnOff 値で上書きされるので、起動時は false 固定。
 */
static bool sSwitchOnOffState = false;

/* =======================================================================
 *  KickAllBindings()
 *      └─ Boot 時、BindingTable の全エントリへ NotifyBoundClusterChanged()
 *         を 1 回発火させるユーティリティ。
 *         Read/Subscribe のトリガである “Bound cluster changed” コールを
 *         疑似的に生成し、HandleBoundDeviceChanged() を呼び出させる。
 * =======================================================================*/
namespace {
void KickAllBindings()
{
    constexpr ClusterId kOnOff = Clusters::OnOff::Id;
    for (const EmberBindingTableEntry & e : BindingTable::GetInstance())
    {
        if (e.type == MATTER_UNICAST_BINDING && e.clusterId.value_or(kOnOff) == kOnOff)
        {
            /* local EP の OnOff が「変わった」ことにして通知 */
            BindingManager::GetInstance().NotifyBoundClusterChanged(e.local, kOnOff, nullptr);
        }
    }
}
} // namespace

/* =======================================================================
 *  SubscribePeerOnOff()
 *      └─ 指定バインディングの相手 EP/Cluster の OnOff 属性を購読する。
 *         a) ReadClient を Subscribe モードで生成
 *         b) MinInterval=0 / MaxInterval=20 / keepSubscriptions=true
 * =======================================================================*/
static void SubscribePeerOnOff(const EmberBindingTableEntry & entry, OperationalDeviceProxy & dev)
{
    /* ---- ReadClient::Callback 実装 ---- */
    class SubCb : public ReadClient::Callback
    {
        /* ReadClient のポインタを保持（削除フックで必要） */
        ReadClient * mClient = nullptr;
      public:
        void Attach(ReadClient * c) { mClient = c; }

        /* 属性値レポートを受信したとき */
        void OnAttributeData(const ConcreteDataAttributePath &,
                             TLV::TLVReader * r,
                             const StatusIB & st) override
        {
            bool v = false;
            if (st.mStatus == Protocols::InteractionModel::Status::Success &&
                r && r->Get(v) == CHIP_NO_ERROR)
            {
                ChipLogProgress(NotSpecified, "[SUB] Peer OnOff = %d", v);
                /* ★必要ならここでローカル状態を合わせる */
                sSwitchOnOffState = v;
            }
        }
        /* ReportData チャンク終了 — Matter1.4 では空実装で可 */
        void OnReportEnd() override {}
        void OnError(CHIP_ERROR e) override
        {
            ChipLogError(NotSpecified, "Subscribe error: %s", ErrorStr(e));
        }
        void OnDone(ReadClient * rc) override
        {
            /* ReadClient のライフサイクル終了：IME から登録解除 */
            InteractionModelEngine::GetInstance()->RemoveReadClient(rc);
        }
    };
    static SubCb sCb; // ★単一購読想定なので static で再利用

    /* ---- ReadClient 生成 ----
     *  コンストラクタで自動的に InteractionModelEngine へ登録される。
     */
    ReadClient * rc = Platform::New<ReadClient>(InteractionModelEngine::GetInstance(),
                                                dev.GetExchangeManager(),
                                                sCb,
                                                ReadClient::InteractionType::Subscribe);
    VerifyOrReturn(rc, ChipLogError(NotSpecified, "ReadClient OOM"));
    sCb.Attach(rc); // コールバックへ逆参照を渡す

    /* ---- Subscribe パラメータ ---- */
    ReadPrepareParams p(dev.GetSecureSession().Value());
    p.mMinIntervalFloorSeconds   = 0;   // 値が変化したら即レポート
    p.mMaxIntervalCeilingSeconds = 20;  // 変化なくても ≤20 s で keep-alive
    p.mKeepSubscriptions         = true;/* 切断 → 再接続時に自動再購読 */

    /* 単一属性 (EP, Cluster, AttributeId) を購読 */
    AttributePathParams path{ entry.remote,
                              Clusters::OnOff::Id,
                              Clusters::OnOff::Attributes::OnOff::Id };
    p.mpAttributePathParamsList    = &path;
    p.mAttributePathParamsListSize = 1;

    /* ---- 送信 ---- */
    CHIP_ERROR err = rc->SendAutoResubscribeRequest(std::move(p));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "Subscribe failed: %s", ErrorStr(err));
    }
}

/* =======================================================================
 *  HandleBoundDeviceChanged()
 *      └─ CASE セッション確立直後、BindingManager 経由で呼ばれる。
 *         - OnOff 目的のユニキャスト Binding のみを対象とする。
 *         - 接続確認後、SubscribePeerOnOff() をコールする。
 * =======================================================================*/
static void HandleBoundDeviceChanged(const EmberBindingTableEntry & binding,
                                     OperationalDeviceProxy       * peerDev,
                                     void *)
{
    /* 条件に合わない Binding は無視 */
    if (binding.type != MATTER_UNICAST_BINDING ||
        binding.local != 1 /* EP-1 */           ||
        binding.clusterId.value_or(Clusters::OnOff::Id) != Clusters::OnOff::Id)
        return;

    VerifyOrReturn(peerDev && peerDev->ConnectionReady(),
                   ChipLogError(NotSpecified, "Peer not ready"));

    /* (1) 相手 OnOff 属性の購読を開始 */
    SubscribePeerOnOff(binding, *peerDev);

    /* (2) ローカル → リモート 同期コマンド
     *     起動直後に相手の状態を上書きしたくないため *無効化*。
     *     必要ならコメントを外して利用してください。
     */

    // auto ok  = [](const ConcreteCommandPath &, const StatusIB &, const auto &) {};
    // auto err = [](CHIP_ERROR e) { ChipLogError(NotSpecified, "Invoke NG: %s", ErrorStr(e)); };

    // if (sSwitchOnOffState)
    // {
    //     Clusters::OnOff::Commands::On::Type cmd;
    //     Controller::InvokeCommandRequest(peerDev->GetExchangeManager(),
    //                                      peerDev->GetSecureSession().Value(),
    //                                      binding.remote, cmd, ok, err);
    // }
    // else
    // {
    //     Clusters::OnOff::Commands::Off::Type cmd;
    //     Controller::InvokeCommandRequest(peerDev->GetExchangeManager(),
    //                                      peerDev->GetSecureSession().Value(),
    //                                      binding.remote, cmd, ok, err);
    // }
}
static void HandleContextRelease(void *) {} // 現状は特に処理なし

/* =======================================================================
 *  InitBindingHandlers()
 *      └─ BindingManager の初期化とハンドラ登録を
 *         Matter Platform スレッドで非同期実行するラッパー。
 * =======================================================================*/
static void InitBindingHandlerInternal(intptr_t)
{
    auto & srv = Server::GetInstance();
    BindingManager::GetInstance().Init(
        { &srv.GetFabricTable(),
          srv.GetCASESessionManager(),
          &srv.GetPersistentStorage() });

    BindingManager::GetInstance().RegisterBoundDeviceChangedHandler(HandleBoundDeviceChanged);
    BindingManager::GetInstance().RegisterBoundDeviceContextReleaseHandler(HandleContextRelease);

    KickAllBindings(); // ★ここで疑似 Notify を発火
}

CHIP_ERROR InitBindingHandlers()
{
    /* Matter スレッド (PlatformMgr) 上で実行 */
    DeviceLayer::PlatformMgr().ScheduleWork(InitBindingHandlerInternal);

#if defined(ENABLE_CHIP_SHELL)
    /* ---- shell コマンド: "switch on|off" ----
     *      仮想スイッチ (EP-1) の OnOff 属性を書き換えて、
     *      BindingManager へ Notify するテスト用。
     */
    using namespace Shell;
    const shell_command_t cmd = {
        [](int argc, char ** argv) -> CHIP_ERROR {
            if (argc == 1 && strcmp(argv[0], "on") == 0)
                sSwitchOnOffState = true;
            else if (argc == 1 && strcmp(argv[0], "off") == 0)
                sSwitchOnOffState = false;
            else
            {
                streamer_printf(streamer_get(), "Usage: switch [on|off]\n");
                return CHIP_NO_ERROR;
            }
            /* ローカル属性が変わった扱いにし、バインディング先へ自動反映 */
            BindingManager::GetInstance().NotifyBoundClusterChanged(
                1 /* EP-1 */, Clusters::OnOff::Id, nullptr);
            return CHIP_NO_ERROR;
        },
        "switch", "switch [on|off]"
    };
    Engine::Root().RegisterCommands(&cmd, 1);
#endif
    return CHIP_NO_ERROR;
}

/* =======================================================================
 *  SwitchOnOffAttributeUpdated()
 *      └─ アプリ側で OnOff 属性を書き換えた際に呼ぶヘルパ。
 *         - ローカル状態を保持し、バインディングを Notify。
 * =======================================================================*/
void SwitchOnOffAttributeUpdated(EndpointId ep, bool value)
{
    sSwitchOnOffState = value;
    BindingManager::GetInstance().NotifyBoundClusterChanged(
        ep, Clusters::OnOff::Id, nullptr);
}

void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value)
{
    using namespace chip::app::Clusters;
    if (attributePath.mClusterId == OnOff::Id &&
        attributePath.mAttributeId == OnOff::Attributes::OnOff::Id)
    {
        ChipLogProgress(NotSpecified, "しにたいたい")
        // bool newVal = *value;
        // SwitchOnOffAttributeUpdated(attributePath.mEndpointId, newVal);
        // ChipLogProgress(NotSpecified, "OnOff changed => %d", newVal);
    }
}

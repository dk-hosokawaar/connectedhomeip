#include <curl/curl.h>                       // libcurl で HTTP GET/POST
#include <string>
#include "aircon_bridge.h"
#include <platform/CHIPDeviceLayer.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <platform/PlatformManager.h>  
#include <lib/support/logging/CHIPLogging.h>

namespace {

constexpr const char * kGetUrl =
    "https://tictest.daikindev.com/aircon/get_sensor_info?"
    "id=mejirodai&spw=lab107108&port=30051";

constexpr const char * kSetUrlFmt =
    "https://tictest.daikindev.com/aircon/set_control_info?"
    "id=mejirodai&spw=lab107108&port=30051"
    "&pow=1&mode=%u&stemp=%d&shum=50&f_rate=1&f_dir_ud=1";

/*── libcurl 用コールバック ───────────────────────────*/
size_t WriteCB(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    static_cast<std::string *>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

/*── 室温を取得し Thermostat.LocalTemperature に書き込む ──*/
void FetchTempAndUpdate(chip::System::Layer *, void * context)
{
    ChipLogProgress(Inet,    "HTTP GET → %s", kGetUrl);
    // 詳細な libcurl トレースが欲しいなら 1 行追加
    CURL *curl   = curl_easy_init();
    std::string body;
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);        // ←コメント解除で全部丸出し
    curl_easy_setopt(curl, CURLOPT_URL, kGetUrl);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCB);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_perform(curl);                 // 実行（簡易エラ処理）
    curl_easy_cleanup(curl);

    size_t pos  = body.find("htemp=");
    float  temp = std::stof(body.substr(pos + 6));      // 例: 25.3
    int16_t value = static_cast<int16_t>(temp * 100);   // 0.01 °C 単位

    ChipLogProgress(Inet, "Parsed temperature = %d (%.2f °C)",              // <- 好きなレベルで
                    static_cast<int>(value), value / 100.0f);


    chip::app::Clusters::Thermostat::Attributes::LocalTemperature::Set(
        /*endpoint*/ 1, value);  // Attribute ID 0x0000 :contentReference[oaicite:1]{index=1}

    /* 30 s 周期で自分自身を再登録＝擬似的な periodic timer */
    chip::DeviceLayer::SystemLayer().StartTimer(
        chip::System::Clock::Seconds32(60), FetchTempAndUpdate, nullptr);  // :contentReference[oaicite:2]{index=2}
}

}

/*── Thermostat の Setpoint 変更を API に転送 ───────────*/
namespace aircon_bridge {           // ★ ここから外部リンケージ
void SendControl(uint8_t mode, int16_t hundredthDegC)
{
    ChipLogError(NotSpecified, "コロムアニキ")
    char url[512];
    std::snprintf(url, sizeof(url), kSetUrlFmt, mode, hundredthDegC / 100);
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);  
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}

void Init()
{
    chip::DeviceLayer::SystemLayer().StartTimer(
        chip::System::Clock::Seconds32(1), FetchTempAndUpdate, nullptr);
}
} // namespace aircon_bridge
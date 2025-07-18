// #if defined(ENABLE_CHIP_SHELL)
// #include <lib/shell/Engine.h>
// #include <lib/support/logging/CHIPLogging.h>
// #include <lib/support/CodeUtils.h>  // ArraySize マクロ用
// using namespace chip::Shell;

// /* ─ command body ─ */
// static CHIP_ERROR CmdArat(int, char **)
// {
//     ChipLogProgress(NotSpecified, "アラタンバリンシャンシャン");
//     return CHIP_NO_ERROR;
// }

// /* ─ registration ─ */
// static const shell_command_t kAratCommands[] = {
//     { CmdArat, "arat", "arat  # アラタンバリンシャンシャンをログに出す" },
//     { nullptr, nullptr, nullptr },
// };

// void RegisterAratCommand()
// {
//     ChipLogProgress(NotSpecified, "助けて");
//     constexpr size_t kCmdCount = ArraySize(kAratCommands) - 1; // ← ★NULL行を除く
//     Engine::Root().RegisterCommands(kAratCommands, kCmdCount);
//     ChipLogProgress(NotSpecified, "きしょい");
// }

// #endif // ENABLE_CHIP_SHELL
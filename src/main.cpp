#include <chrono>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>
#include "robot/Robot.h"

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace {

std::vector<std::string> split_utf8(std::string_view text) {
    std::vector<std::string> chunks;
    chunks.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t len = 1;
        if ((lead & 0x80u) == 0x00u)      len = 1;
        else if ((lead & 0xE0u) == 0xC0u) len = 2;
        else if ((lead & 0xF0u) == 0xE0u) len = 3;
        else if ((lead & 0xF8u) == 0xF0u) len = 4;
        else                              len = 1;

        if (i + len > text.size()) {
            len = 1;
        } else {
            for (std::size_t k = 1; k < len; ++k) {
                if ((static_cast<unsigned char>(text[i + k]) & 0xC0u) != 0x80u) {
                    len = 1;
                    break;
                }
            }
        }

        chunks.emplace_back(text.substr(i, len));
        i += len;
    }
    return chunks;
}

}

int main(int argc, char** argv) {
    #if defined(_WIN32)
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    #endif

    auto trim = [](std::string_view sv) -> std::string_view {
        auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!sv.empty() && is_space(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
        while (!sv.empty() && is_space(static_cast<unsigned char>(sv.back())))  sv.remove_suffix(1);
        return sv;
    };

    auto parse_non_negative_int = [&trim](std::string_view sv, int& out) -> bool {
        sv = trim(sv);
        if (sv.empty()) return false;
        int value = 0;
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
        if (ec != std::errc{} || ptr != sv.data() + sv.size() || value < 0) return false;
        out = value;
        return true;
    };

    int delay_from_cli = -1;
    int cps_from_cli = -1;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--delay" && i + 1 < argc) {
            if (parse_non_negative_int(argv[++i], delay_from_cli)) continue;
            std::cerr << "--delay 需要一个非负整数\n";
            return 1;
        }
        if (arg == "--cps" && i + 1 < argc) {
            if (parse_non_negative_int(argv[++i], cps_from_cli)) continue;
            std::cerr << "--cps 需要一个非负整数\n";
            return 1;
        }
    }

    int countdown_seconds = 0;
    if (delay_from_cli >= 0) {
        countdown_seconds = delay_from_cli;
        std::cout << "使用命令行指定的倒计时: " << countdown_seconds << " 秒\n";
    } else {
        while (true) {
            std::cout << "未通过 --delay 指定倒计时，请输入倒计时秒数（非负整数）: " << std::flush;
            std::string line;
            if (!std::getline(std::cin, line)) {
                std::cerr << "\n读取倒计时失败。\n";
                return 1;
            }
            int seconds = 0;
            if (parse_non_negative_int(line, seconds)) {
                countdown_seconds = seconds;
                break;
            }
            std::cout << "输入无效，请输入一个非负整数。\n";
        }
    }

    std::string text_to_type;
    std::cout << "请输入要自动写入的文本: " << std::flush;
    if (!std::getline(std::cin, text_to_type)) {
        std::cerr << "读取文本失败。\n";
        return 1;
    }

    int cps = cps_from_cli;
    if (cps < 0) {
        while (true) {
            std::cout << "请输入输入速度（字符/秒，0 表示不限速）: " << std::flush;
            std::string line;
            if (!std::getline(std::cin, line)) {
                std::cerr << "\n读取输入速度失败。\n";
                return 1;
            }
            int value = 0;
            if (parse_non_negative_int(line, value)) {
                cps = value;
                break;
            }
            std::cout << "输入无效，请输入一个非负整数。\n";
        }
    }

    if (countdown_seconds > 0) {
        std::cout << "--- 倒计时开始 ---\n"
                  << "请在倒计时结束前，将光标聚焦到目标输入框...\n";
        for (int i = countdown_seconds; i > 0; --i) {
            std::cout << i << "... " << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::cout << "\n倒计时结束，开始输入！\n";
    } else {
        std::cout << "倒计时为 0 秒，立即开始输入。请确保目标输入框已聚焦！\n";
    }

    auto session_result = robot::Session::create();
    if (!session_result) {
        std::cerr << "创建 Session 失败: " << session_result.error().message << '\n';
        return 1;
    }
    auto session = std::move(*session_result);

    if (!session->capabilities().canInjectKeyboard) {
        std::cerr << "当前环境不支持键盘输入注入。\n";
        return 1;
    }

    if (cps > 0) {
        std::cout << "输入速度：" << cps << " 字符/秒\n";
    } else {
        std::cout << "输入速度：不限速\n";
    }

    const std::vector<std::string> chunks = split_utf8(text_to_type);

    using clock = std::chrono::steady_clock;
    const std::chrono::duration<double> interval =
        (cps > 0) ? std::chrono::duration<double>(1.0 / static_cast<double>(cps))
                  : std::chrono::duration<double>(0.0);
    const auto interval_dur = std::chrono::duration_cast<clock::duration>(interval);
    auto deadline = clock::now();

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (i > 0 && cps > 0) {
            deadline += interval_dur;
            const auto now = clock::now();
            if (deadline < now) {
                deadline = now;
            } else {
                std::this_thread::sleep_until(deadline);
            }
        }

        if (auto result = session->keyboard().typeText(chunks[i]); !result) {
            std::cerr << "文本输入失败: " << result.error().message << '\n';
            return 1;
        }
    }

    std::cout << "文本输入完成。\n";
    return 0;
}

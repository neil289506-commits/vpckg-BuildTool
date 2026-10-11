#pragma once
#include <QString>

// 唯一對外介面。真正的 PAT 實作放在 PatProvider_private.cpp（不開源、已被 .gitignore 排除）。
namespace PatProvider {
QString token();
}

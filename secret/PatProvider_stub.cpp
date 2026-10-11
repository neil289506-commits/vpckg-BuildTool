// 公開的空殼：沒有私有實作時使用，從環境變數 VCPKG_COMPILE_PAT 讀取（開發/測試用）。
#include "PatProvider.h"

QString PatProvider::token()
{
    return qEnvironmentVariable("VCPKG_COMPILE_PAT");
}

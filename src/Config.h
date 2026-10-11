#pragma once
#include <QString>

// 公開設定（不含任何機密）。倉庫名稱若有變動只需改這裡。
namespace Config {
inline const QString kRepoOwner    = QStringLiteral("neil289506-commits");
inline const QString kRepoName     = QStringLiteral("vpckg-atcompile");
inline const QString kWorkflowFile = QStringLiteral("build.yml");
inline const QString kWorkflowPath = QStringLiteral(".github/workflows/build.yml");
inline const QString kVcpkgGitUrl  = QStringLiteral("https://github.com/microsoft/vcpkg.git");
}

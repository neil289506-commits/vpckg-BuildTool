#include "MainWindow.h"

#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QStringListModel>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("vcpkg 雲端編譯工具"));
    resize(880, 680);
    buildUi();

    // ---- vcpkg 本機環境 ----
    connect(&m_vcpkg, &VcpkgManager::log, this, [this](const QString& s) { m_localLog->appendPlainText(s); });
    connect(&m_vcpkg, &VcpkgManager::readyChanged, this, [this](bool ok, const QString& msg) {
        m_vcpkgReady = ok;
        m_vcpkgStatus->setText((ok ? QStringLiteral("✔ ") : QStringLiteral("… ")) + msg);
        m_package->setPlaceholderText(ok ? QStringLiteral("輸入函式庫名稱，例如 zlib、openssl、boost-asio")
                                         : QStringLiteral("輸入函式庫名稱（候補功能待 vcpkg 就緒後啟用）"));
    });
    connect(&m_vcpkg, &VcpkgManager::searchFinished, this, [this](const QString& query, const QStringList& names) {
        if (query != m_package->text().trimmed())
            return; // 已過期的結果
        m_model->setStringList(names);
        if (!names.isEmpty() && m_package->hasFocus())
            m_completer->complete();
    });
    connect(m_recheck, &QPushButton::clicked, this, [this]() { m_vcpkg.ensureReady(); });

    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(350);
    connect(&m_searchTimer, &QTimer::timeout, this, [this]() {
        const QString q = m_package->text().trimmed();
        if (m_vcpkgReady && q.size() >= 2)
            m_vcpkg.search(q);
    });
    connect(m_package, &QLineEdit::textEdited, this, [this]() { m_searchTimer.start(); });

    // ---- 編譯流程 ----
    connect(&m_build, &BuildController::phaseChanged, this, [this](const QString& s) { m_phase->setText(s); });
    connect(&m_build, &BuildController::progressChanged, this, [this](int v) { m_bar->setValue(v); });
    connect(&m_build, &BuildController::stepsChanged, this, [this](const QList<StepInfo>& s) { updateSteps(s); });
    connect(&m_build, &BuildController::logTailChanged, this, [this](const QString& t) {
        m_cloudLog->setPlainText(t);
        m_cloudLog->verticalScrollBar()->setValue(m_cloudLog->verticalScrollBar()->maximum());
    });
    connect(&m_build, &BuildController::finished, this,
            [this](bool ok, const QString& msg, const QString& file) {
                setBusy(false);
                m_phase->setText(msg);
                if (msg == QStringLiteral("已取消"))
                    return;
                if (ok)
                    m_bar->setValue(100);
                QMessageBox box(ok ? QMessageBox::Information : QMessageBox::Warning,
                                ok ? QStringLiteral("編譯完成") : QStringLiteral("編譯未成功"), msg,
                                QMessageBox::Ok, this);
                QPushButton* open = nullptr;
                if (!file.isEmpty())
                    open = box.addButton(QStringLiteral("開啟資料夾"), QMessageBox::ActionRole);
                box.exec();
                if (open && box.clickedButton() == open)
                    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath()));
            });

    QTimer::singleShot(0, this, [this]() { m_vcpkg.ensureReady(); });
}

void MainWindow::buildUi()
{
    QSettings st;
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    // vcpkg 環境
    auto* envBox = new QGroupBox(QStringLiteral("本機 vcpkg（僅用於搜尋候補）"), central);
    auto* envLay = new QHBoxLayout(envBox);
    m_vcpkgStatus = new QLabel(QStringLiteral("檢查中…"), envBox);
    m_recheck = new QPushButton(QStringLiteral("重新檢查 / 初始化"), envBox);
    envLay->addWidget(m_vcpkgStatus, 1);
    envLay->addWidget(m_recheck);
    root->addWidget(envBox);

    // 編譯設定
    auto* cfgBox = new QGroupBox(QStringLiteral("編譯設定"), central);
    auto* form = new QFormLayout(cfgBox);

    m_package = new QLineEdit(cfgBox);
    m_model = new QStringListModel(this);
    m_completer = new QCompleter(m_model, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setMaxVisibleItems(12);
    m_package->setCompleter(m_completer);
    form->addRow(QStringLiteral("函式庫名稱"), m_package);

    m_triplet = new QComboBox(cfgBox);
    m_triplet->setEditable(true);
    m_triplet->addItems({QStringLiteral("x64-windows"), QStringLiteral("x64-windows-static"),
                         QStringLiteral("x64-windows-static-md"), QStringLiteral("x86-windows"),
                         QStringLiteral("x86-windows-static"), QStringLiteral("arm64-windows")});
    m_triplet->setCurrentText(st.value(QStringLiteral("triplet"), QStringLiteral("x64-windows")).toString());
    form->addRow(QStringLiteral("Triplet"), m_triplet);

    m_features = new QLineEdit(cfgBox);
    m_features->setPlaceholderText(QStringLiteral("選填，以逗號分隔，例如 ssl,zlib"));
    form->addRow(QStringLiteral("Features"), m_features);

    m_extra = new QLineEdit(cfgBox);
    m_extra->setPlaceholderText(QStringLiteral("選填，附加 vcpkg install 參數，例如 --recurse"));
    form->addRow(QStringLiteral("附加參數"), m_extra);

    auto* dirRow = new QHBoxLayout;
    m_saveDir = new QLineEdit(cfgBox);
    m_saveDir->setText(st.value(QStringLiteral("saveDir"),
                                QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
                           .toString());
    m_browse = new QPushButton(QStringLiteral("瀏覽…"), cfgBox);
    dirRow->addWidget(m_saveDir, 1);
    dirRow->addWidget(m_browse);
    form->addRow(QStringLiteral("下載位置"), dirRow);
    root->addWidget(cfgBox);
    connect(m_browse, &QPushButton::clicked, this, [this]() { onBrowse(); });

    // 按鈕
    auto* btnRow = new QHBoxLayout;
    m_start = new QPushButton(QStringLiteral("開始編譯"), central);
    m_cancel = new QPushButton(QStringLiteral("取消"), central);
    m_cancel->setEnabled(false);
    btnRow->addStretch(1);
    btnRow->addWidget(m_start);
    btnRow->addWidget(m_cancel);
    root->addLayout(btnRow);
    connect(m_start, &QPushButton::clicked, this, [this]() { onStart(); });
    connect(m_cancel, &QPushButton::clicked, this, [this]() { m_build.cancel(); });

    // 進度
    m_phase = new QLabel(QStringLiteral("待命"), central);
    m_bar = new QProgressBar(central);
    m_bar->setRange(0, 100);
    root->addWidget(m_phase);
    root->addWidget(m_bar);

    auto* tabs = new QTabWidget(central);
    m_steps = new QListWidget(tabs);
    m_cloudLog = new QPlainTextEdit(tabs);
    m_localLog = new QPlainTextEdit(tabs);
    for (QPlainTextEdit* e : {m_cloudLog, m_localLog}) {
        e->setReadOnly(true);
        e->setLineWrapMode(QPlainTextEdit::NoWrap);
        e->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    }
    tabs->addTab(m_steps, QStringLiteral("雲端步驟"));
    tabs->addTab(m_cloudLog, QStringLiteral("雲端 Log"));
    tabs->addTab(m_localLog, QStringLiteral("本機 Log"));
    root->addWidget(tabs, 1);

    setCentralWidget(central);
}

void MainWindow::onBrowse()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("選擇下載位置"), m_saveDir->text());
    if (!dir.isEmpty())
        m_saveDir->setText(QDir::toNativeSeparators(dir));
}

void MainWindow::setBusy(bool busy)
{
    m_start->setEnabled(!busy);
    m_cancel->setEnabled(busy);
    for (QWidget* w : {static_cast<QWidget*>(m_package), static_cast<QWidget*>(m_triplet),
                       static_cast<QWidget*>(m_features), static_cast<QWidget*>(m_extra),
                       static_cast<QWidget*>(m_saveDir), static_cast<QWidget*>(m_browse)})
        w->setEnabled(!busy);
}

void MainWindow::onStart()
{
    static const QRegularExpression rePkg(QStringLiteral("^[a-z0-9][a-z0-9\\-]*$"));
    static const QRegularExpression reTriplet(QStringLiteral("^[a-z0-9\\-]+$"));
    static const QRegularExpression reFeatures(QStringLiteral("^[a-z0-9\\-,]*$"));
    static const QRegularExpression reExtra(QStringLiteral("^[A-Za-z0-9 _\\-=.:/\\[\\]]*$"));

    BuildRequest req;
    req.package = m_package->text().trimmed().toLower();
    req.triplet = m_triplet->currentText().trimmed();
    req.features = m_features->text().trimmed().remove(QLatin1Char(' '));
    req.extraArgs = m_extra->text().trimmed();
    req.saveDir = QDir::fromNativeSeparators(m_saveDir->text().trimmed());

    auto bad = [this](const QString& msg) { QMessageBox::warning(this, QStringLiteral("參數錯誤"), msg); };
    if (!reTriplet.match(req.triplet).hasMatch()) return bad(QStringLiteral("Triplet 格式不正確"));
    if (!rePkg.match(req.package).hasMatch()) return bad(QStringLiteral("函式庫名稱只能包含小寫英數與 -"));
    if (!reFeatures.match(req.features).hasMatch()) return bad(QStringLiteral("Features 只能包含小寫英數、- 與逗號"));
    if (!reExtra.match(req.extraArgs).hasMatch()) return bad(QStringLiteral("附加參數含有不允許的字元"));
    if (req.saveDir.isEmpty()) return bad(QStringLiteral("請選擇下載位置"));

    QSettings st;
    st.setValue(QStringLiteral("triplet"), req.triplet);
    st.setValue(QStringLiteral("saveDir"), m_saveDir->text().trimmed());

    m_steps->clear();
    m_cloudLog->clear();
    m_bar->setValue(0);
    setBusy(true);
    m_build.start(req);
    if (!m_build.busy()) // 立即失敗（例如沒有 PAT）時 finished 已處理
        setBusy(false);
}

void MainWindow::updateSteps(const QList<StepInfo>& steps)
{
    m_steps->clear();
    for (const StepInfo& s : steps) {
        QString icon = QStringLiteral("○");
        if (s.status == QLatin1String("in_progress"))
            icon = QStringLiteral("▶");
        else if (s.status == QLatin1String("completed")) {
            if (s.conclusion == QLatin1String("success")) icon = QStringLiteral("✔");
            else if (s.conclusion == QLatin1String("skipped")) icon = QStringLiteral("–");
            else icon = QStringLiteral("✖");
        }
        m_steps->addItem(icon + QLatin1Char(' ') + s.name);
    }
}

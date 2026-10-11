#pragma once
#include "BuildController.h"
#include "VcpkgManager.h"

#include <QMainWindow>
#include <QTimer>

class QComboBox;
class QCompleter;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStringListModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void onStart();
    void onBrowse();
    void setBusy(bool busy);
    void updateSteps(const QList<StepInfo>& steps);

    VcpkgManager m_vcpkg;
    BuildController m_build;
    QTimer m_searchTimer;
    bool m_vcpkgReady = false;

    QLabel* m_vcpkgStatus = nullptr;
    QPushButton* m_recheck = nullptr;
    QLineEdit* m_package = nullptr;
    QComboBox* m_triplet = nullptr;
    QLineEdit* m_features = nullptr;
    QLineEdit* m_extra = nullptr;
    QLineEdit* m_saveDir = nullptr;
    QPushButton* m_browse = nullptr;
    QPushButton* m_start = nullptr;
    QPushButton* m_cancel = nullptr;
    QLabel* m_phase = nullptr;
    QProgressBar* m_bar = nullptr;
    QListWidget* m_steps = nullptr;
    QPlainTextEdit* m_cloudLog = nullptr;
    QPlainTextEdit* m_localLog = nullptr;
    QStringListModel* m_model = nullptr;
    QCompleter* m_completer = nullptr;
};

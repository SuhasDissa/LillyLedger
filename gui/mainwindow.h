#pragma once

#include "core/enum/instrument.h"
#include "core/enum/side.h"
#include "core/enum/status.h"
#include "core/executionreport.h"
#include <QMainWindow>
#include <QPalette>
#include <QString>
#include <QtGlobal>
#include <cstddef>
#include <vector>

class QAction;
class QDockWidget;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QTableWidget;
class QTabWidget;

Q_DECLARE_METATYPE(Instrument)

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);

  signals:
    void currentInstrumentChanged(Instrument instrument);

  public slots:
    void loadInputFile(const QString &filePath);
    void runEngine();
    void exportResults(const QString &filePath);

  private:
    enum class LedState { Idle, Running, Error };

    void setupCentralTabs();
    void setupDocks();
    void setupMenuBar();
    void setupToolBar();
    void applyDarkTheme();
    void applyLightTheme();
    void setLedState(LedState state);
    void refreshUiState();
    void updateExecutionReportsTable();
    void updateOrderBookTab();
    Instrument selectedInstrument() const;

    static QString instrumentName(Instrument instrument);
    static QString sideName(Side side);
    static QString statusName(Status status);
    static QString fixedCharToQString(const char *text, std::size_t maxLen);

    QTabWidget *tabWidget_{nullptr};
    QTableWidget *executionReportsTable_{nullptr};
    QPlainTextEdit *orderBookSummary_{nullptr};

    QDockWidget *instrumentDock_{nullptr};
    QDockWidget *statusDock_{nullptr};
    QListWidget *instrumentList_{nullptr};

    QLabel *ordersProcessedValue_{nullptr};
    QLabel *reportsGeneratedValue_{nullptr};
    QLabel *lastRunDurationValue_{nullptr};
    QLabel *ledIndicator_{nullptr};
    QLabel *engineStateValue_{nullptr};

    QLabel *perfOrdersValue_{nullptr};
    QLabel *perfReportsValue_{nullptr};
    QLabel *perfDurationValue_{nullptr};

    QAction *runEngineAction_{nullptr};
    QAction *exportResultsAction_{nullptr};
    QAction *toggleThemeAction_{nullptr};
    QAction *toggleDockPanelsAction_{nullptr};

    QString inputFilePath_;
    std::vector<ExecutionReport> executionReports_;
    std::size_t ordersProcessed_{0};
    qint64 lastRunDurationMs_{0};
    QPalette lightPalette_;
    bool darkThemeEnabled_{true};
};

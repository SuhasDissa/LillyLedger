#pragma once

#include "core/enum/instrument.h"
#include "core/enum/side.h"
#include "core/enum/status.h"
#include "core/executionreport.h"
#include "engine/orderrouter.h"
#include <QMainWindow>
#include <QString>
#include <QtGlobal>
#include <cstddef>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class ManualOrderEntryWidget;
class OrderBookWidget;
class PerformanceDashboard;

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

    void setupSidebar();
    void setupTopBar();
    void setupBottomBar();
    void setupCentralArea();
    void applyLightTheme();
    void setLedState(LedState state);
    void refreshUiState();
    void updateExecutionReportsTable();
    void updateOrderBookTab();
    void updatePerformanceTab(qint64 parseUs, qint64 matchUs, qint64 writeUs);
    void setCurrentPage(int index);
    void updateSummaryBar(int filteredCount);
    Instrument selectedInstrument() const;

    static QString instrumentName(Instrument instrument);
    static QString sideName(Side side);
    static QString statusName(Status status);
    static QString compactTransactTime(const char *text);
    static QString fixedCharToQString(const char *text, std::size_t maxLen);

    QStackedWidget *stackedWidget_{nullptr};
    OrderBookWidget *orderBookWidget_{nullptr};
    QTableWidget *executionReportsTable_{nullptr};
    ManualOrderEntryWidget *manualEntryWidget_{nullptr};
    PerformanceDashboard *performanceDashboard_{nullptr};

    QWidget *sidebarWidget_{nullptr};
    QWidget *topBarWidget_{nullptr};
    QWidget *bottomBarWidget_{nullptr};
    QPushButton *sidebarInstrumentsButton_{nullptr};
    QPushButton *sidebarAnalyticsButton_{nullptr};
    QPushButton *sidebarManualButton_{nullptr};
    QPushButton *sidebarPerformanceButton_{nullptr};
    QPushButton *sidebarSupportButton_{nullptr};
    QComboBox *orderBookInstrumentCombo_{nullptr};
    QComboBox *instrumentFilter_{nullptr};
    QComboBox *statusFilter_{nullptr};
    QComboBox *sideFilter_{nullptr};

    QPushButton *openCsvButton_{nullptr};
    QPushButton *runEngineButton_{nullptr};
    QPushButton *exportCsvButton_{nullptr};
    std::vector<QPushButton *> topTabButtons_;

    QLabel *reportsSubtitleLabel_{nullptr};
    QLabel *showingResultsLabel_{nullptr};
    QLabel *summaryTotalValue_{nullptr};
    QLabel *summaryFilledValue_{nullptr};
    QLabel *summaryPartialValue_{nullptr};
    QLabel *summaryRejectedValue_{nullptr};
    QLabel *summaryNewValue_{nullptr};

    QLabel *ordersProcessedValue_{nullptr};
    QLabel *reportsGeneratedValue_{nullptr};
    QLabel *lastRunDurationValue_{nullptr};
    QLabel *engineStateValue_{nullptr};

    QString inputFilePath_;
    std::vector<ExecutionReport> executionReports_;
    OrderRouter router_;
    std::size_t ordersProcessed_{0};
    qint64 lastRunDurationMs_{0};
};

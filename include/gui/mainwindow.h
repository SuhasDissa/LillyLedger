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

class QAbstractTableModel;

class QStackedWidget;

class ManualOrderEntryWidget;
class OrderBookWidget;
class PerformanceDashboard;

namespace Ui {
class MainWindow;
}

Q_DECLARE_METATYPE(Instrument)

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

  signals:
    void currentInstrumentChanged(Instrument instrument);

  public slots:
    void loadInputFile(const QString &filePath);
    void runEngine();
    void exportResults(const QString &filePath);

  private:
    enum class LedState { Idle, Running, Error };

    void applyLightTheme();
    void setLedState(LedState state);
    void refreshUiState();
    void updateExecutionReportsTable();
    void updateOrderBookTab();
    void updatePerformanceTab(qint64 parseUs, qint64 matchUs, qint64 writeUs);
    void setCurrentPage(int index);
    void updateSummaryBar(int filteredCount);
    Instrument selectedInstrument() const;

    Ui::MainWindow *ui_;
    QAbstractTableModel *reportsModel_{nullptr};

    QString inputFilePath_;
    std::vector<ExecutionReport> executionReports_;
    OrderRouter router_;
    std::size_t ordersProcessed_{0};
    qint64 lastRunDurationMs_{0};
};

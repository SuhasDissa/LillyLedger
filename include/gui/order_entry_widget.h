#pragma once

#include "core/enum/side.h"
#include "core/order.h"
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QGraphicsOpacityEffect;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class BuySellToggle : public QWidget {
    Q_OBJECT

  public:
    explicit BuySellToggle(QWidget *parent = nullptr);

    Side side() const;
    void setSide(Side side);

  signals:
    void sideChanged(Side side);

  private:
    void updateStyles();

    QPushButton *buyButton_{nullptr};
    QPushButton *sellButton_{nullptr};
    Side currentSide_{Side::Buy};
};

namespace Ui {
class ManualOrderEntryWidget;
}

class ManualOrderEntryWidget : public QWidget {
    Q_OBJECT

  public:
    explicit ManualOrderEntryWidget(QWidget *parent = nullptr);
    ~ManualOrderEntryWidget();

  public slots:
    void prefillPrice(double price, Side side);

  signals:
    void orderSubmitted(const Order &order);

  private slots:
    void onSubmitClicked();
    void onResetClicked();

  private:
    void setupConnections();
    void resetForm();
    bool validateForm();
    void clearValidationErrors();
    void setFieldError(QWidget *field, const QString &message);
    void runSubmitShakeAnimation();
    void showSuccessMessage();

    static Instrument instrumentFromComboIndex(int index);

    Ui::ManualOrderEntryWidget *ui_;
    QGraphicsOpacityEffect *successOpacityEffect_{nullptr};
    QTimer *successHoldTimer_{nullptr};
};

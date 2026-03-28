#pragma once

#include "core/enum/side.h"
#include "core/order.h"
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGraphicsOpacityEffect;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QVBoxLayout;

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

class ManualOrderEntryWidget : public QWidget {
    Q_OBJECT

  public:
    explicit ManualOrderEntryWidget(QWidget *parent = nullptr);

  public slots:
    void prefillPrice(double price, Side side);

  signals:
    void orderSubmitted(const Order &order);

  private slots:
    void onSubmitClicked();
    void onResetClicked();

  private:
    void setupUi();
    void setupConnections();
    void updateOrderIdPreview();
    QString previewOrderId() const;
    void resetForm();
    bool validateForm();
    void clearValidationErrors();
    void setFieldError(QWidget *field, const QString &message);
    void runSubmitShakeAnimation();
    void showSuccessMessage();

    static Instrument instrumentFromComboIndex(int index);

    QGroupBox *orderGroup_{nullptr};
    QLineEdit *clientIdEdit_{nullptr};
    QComboBox *instrumentCombo_{nullptr};
    BuySellToggle *sideToggle_{nullptr};
    QWidget *quantityContainer_{nullptr};
    QSpinBox *quantitySpin_{nullptr};
    QLabel *quantityErrorLabel_{nullptr};
    QDoubleSpinBox *priceSpin_{nullptr};
    QLineEdit *orderIdPreviewEdit_{nullptr};
    QPushButton *submitButton_{nullptr};
    QPushButton *resetButton_{nullptr};
    QLabel *successLabel_{nullptr};
    QGraphicsOpacityEffect *successOpacityEffect_{nullptr};
    QTimer *successHoldTimer_{nullptr};

    int nextOrderCounter_{1};
};

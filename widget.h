#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QMap>

QT_BEGIN_NAMESPACE
<<<<<<< HEAD
namespace Ui { class Widget; }
=======
>>>>>>> 19d33f3 (added calender system)
class QFormLayout;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QDateEdit;
class QTimeEdit;
class QLineEdit;
class QCheckBox;
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT
public:
    explicit Widget(QWidget *parent = nullptr);
<<<<<<< HEAD
    ~Widget() override;
=======
    ~Widget() override = default;
>>>>>>> 19d33f3 (added calender system)

private:
    QSpinBox *addInteger(QFormLayout *form, const QString &key, const QString &label,
                         int minimum, int maximum, int value);
    QDoubleSpinBox *addDecimal(QFormLayout *form, const QString &key,
                               const QString &label, double value);
    QComboBox *addChoice(QFormLayout *form, const QString &key, const QString &label,
                         const QStringList &labels, const QStringList &values, int index);
    void resetDefaults();
    void updateSummary();
    QStringList validationErrors() const;

<<<<<<< HEAD
    Ui::Widget *ui;
=======
>>>>>>> 19d33f3 (added calender system)
    QMap<QString, QSpinBox *> integers;
    QMap<QString, QDoubleSpinBox *> decimals;
    QMap<QString, QComboBox *> choices;
    QList<QCheckBox *> workingDays;
    QDateEdit *startDate;
    QTimeEdit *workStart;
    QTimeEdit *workEnd;
    QLineEdit *seed;
    QLabel *summary = nullptr;
    QLabel *status = nullptr;
};
#endif

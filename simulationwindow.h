#ifndef SIMULATIONWINDOW_H
#define SIMULATIONWINDOW_H

#include <QDate>
#include <QList>
#include <QWidget>

class SimulationWindow : public QWidget
{
public:
    explicit SimulationWindow(const QDate &startDate, int dayCount,
                              const QList<bool> &workingDays, QWidget *parent = nullptr);
};

#endif

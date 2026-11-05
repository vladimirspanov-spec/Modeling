#pragma once
#include "src/core/experimentrunner.h"
#include <QDialog>

class ExperimentDialog : public QDialog {
public:
    explicit ExperimentDialog(const CalendarSetup &setup, QWidget *parent = nullptr);
private:
    ExperimentRunner runner_;
};

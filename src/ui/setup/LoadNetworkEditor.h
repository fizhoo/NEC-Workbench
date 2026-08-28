#pragma once

#include "model/AntennaModel.h"
#include "model/ModelSetup.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace necwb::ui {

class LoadNetworkEditor final : public QWidget {
    Q_OBJECT
public:
    explicit LoadNetworkEditor(QWidget* parent = nullptr);
    void setData(const model::AntennaModel& model, const model::ModelSetup& setup);
signals:
    void loadChanged(model::LoadDefinition load);
    void loadDeleteRequested(std::size_t sourceLine);
    void transmissionLineChanged(model::TransmissionLineDefinition line);
    void transmissionLineDeleteRequested(std::size_t sourceLine);
private:
    void applySelectedLoad();
    void applySelectedLine();
    model::AntennaModel model_;
    QTableWidget* loads_{};
    QTableWidget* lines_{};
    QLabel* validation_{};
};

}

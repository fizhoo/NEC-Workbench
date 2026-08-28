#pragma once

#include "model/Point3D.h"

#include <QUndoCommand>

#include <functional>

namespace necwb::ui {

class MoveEndpointCommand final : public QUndoCommand {
public:
    using ApplyFunction = std::function<void(const model::Point3D&)>;

    MoveEndpointCommand(QString description, model::Point3D original, model::Point3D updated,
        ApplyFunction apply, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    model::Point3D original_;
    model::Point3D updated_;
    ApplyFunction apply_;
};

}

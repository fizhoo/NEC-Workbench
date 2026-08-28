#pragma once

#include "model/Point3D.h"

#include <QUndoCommand>

#include <functional>

namespace necwb::ui {

class MoveWireCommand final : public QUndoCommand {
public:
    using ApplyFunction = std::function<void(const model::Point3D&, const model::Point3D&)>;

    MoveWireCommand(QString description, model::Point3D originalStart, model::Point3D originalEnd,
        model::Point3D updatedStart, model::Point3D updatedEnd, ApplyFunction apply,
        QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    model::Point3D originalStart_;
    model::Point3D originalEnd_;
    model::Point3D updatedStart_;
    model::Point3D updatedEnd_;
    ApplyFunction apply_;
};

}

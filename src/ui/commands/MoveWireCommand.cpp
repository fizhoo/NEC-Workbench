#include "ui/commands/MoveWireCommand.h"

#include <utility>

namespace necwb::ui {

MoveWireCommand::MoveWireCommand(QString description, model::Point3D originalStart,
    model::Point3D originalEnd, model::Point3D updatedStart, model::Point3D updatedEnd,
    ApplyFunction apply, QUndoCommand* parent)
    : QUndoCommand(std::move(description), parent)
    , originalStart_(originalStart)
    , originalEnd_(originalEnd)
    , updatedStart_(updatedStart)
    , updatedEnd_(updatedEnd)
    , apply_(std::move(apply))
{
}

void MoveWireCommand::undo()
{
    apply_(originalStart_, originalEnd_);
}

void MoveWireCommand::redo()
{
    apply_(updatedStart_, updatedEnd_);
}

}

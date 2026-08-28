#include "ui/commands/MoveEndpointCommand.h"

#include <utility>

namespace necwb::ui {

MoveEndpointCommand::MoveEndpointCommand(QString description, model::Point3D original,
    model::Point3D updated, ApplyFunction apply, QUndoCommand* parent)
    : QUndoCommand(std::move(description), parent)
    , original_(original)
    , updated_(updated)
    , apply_(std::move(apply))
{
}

void MoveEndpointCommand::undo()
{
    apply_(original_);
}

void MoveEndpointCommand::redo()
{
    apply_(updated_);
}

}

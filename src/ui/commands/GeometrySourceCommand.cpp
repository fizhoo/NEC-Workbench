#include "ui/commands/GeometrySourceCommand.h"

#include <utility>

namespace necwb::ui {

GeometrySourceCommand::GeometrySourceCommand(QString description, QString original, QString updated,
    ApplyFunction apply, QUndoCommand* parent)
    : QUndoCommand(std::move(description), parent)
    , original_(std::move(original))
    , updated_(std::move(updated))
    , apply_(std::move(apply))
{
}

void GeometrySourceCommand::undo()
{
    apply_(original_);
}

void GeometrySourceCommand::redo()
{
    apply_(updated_);
}

}

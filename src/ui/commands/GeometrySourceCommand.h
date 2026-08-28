#pragma once

#include <QString>
#include <QUndoCommand>

#include <functional>

namespace necwb::ui {

class GeometrySourceCommand final : public QUndoCommand {
public:
    using ApplyFunction = std::function<void(const QString&)>;

    GeometrySourceCommand(QString description, QString original, QString updated,
        ApplyFunction apply, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    QString original_;
    QString updated_;
    ApplyFunction apply_;
};

}

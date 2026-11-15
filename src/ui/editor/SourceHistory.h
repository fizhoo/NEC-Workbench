#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

class QAction;
class QWidget;

namespace necwb::ui {

class NecEditor;

// The model's undo history: the raw source's own undo stack, with what each step was and where
// it was made. Ctrl+Z and Ctrl+Y (or Ctrl+Shift+Z) reach it from anywhere in the window except
// while typing in the raw source, which keeps the editor's own undo.
class SourceHistory final : public QObject {
public:
    struct Entry {
        QString description;
        // Where to go back to on undo; the main window gives these their meaning, and -1 is
        // the raw source.
        int destination{-1};
        int geometryTab{};
        int sourceTab{};
    };

    SourceHistory(NecEditor* editor, QWidget* window);
    ~SourceHistory() override;

    [[nodiscard]] auto undoAction() const -> QAction* { return undo_; }
    [[nodiscard]] auto redoAction() const -> QAction* { return redo_; }

    // Replaces the whole source as one undo step called description.
    void replaceSource(const Entry& entry, const QString& source);
    // Forgets every step, as for a model just opened.
    void clear();
    // Called after an undo (true) or redo (false), with the step undone or redone.
    void setStepCallback(std::function<void(const Entry&, bool)> callback);
    // Asked before an undo from the keyboard: true when it reverted a value typed but not
    // yet applied, which takes that Ctrl+Z.
    void setRevertTypingCallback(std::function<bool()> callback);

protected:
    auto eventFilter(QObject* watched, QEvent* event) -> bool override;

private:
    void step(bool undo);
    void record();
    void updateActions();

    NecEditor* editor_{};
    QWidget* window_{};
    QAction* undo_{};
    QAction* redo_{};
    std::optional<Entry> pending_;
    std::vector<Entry> undone_;
    std::vector<Entry> done_;
    bool stepping_{};
    std::function<void(const Entry&, bool)> stepped_;
    std::function<bool()> revertTyping_;
};

}

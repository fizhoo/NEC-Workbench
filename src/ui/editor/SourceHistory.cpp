#include "ui/editor/SourceHistory.h"

#include "ui/WorkbenchIcon.h"
#include "ui/editor/NecEditor.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QTextDocument>

namespace necwb::ui {

SourceHistory::SourceHistory(NecEditor* editor, QWidget* window)
    : QObject(window), editor_(editor), window_(window)
{
    undo_ = new QAction(tr("&Undo"), this);
    undo_->setObjectName(QStringLiteral("undoAction"));
    undo_->setIcon(workbenchIcon("undo"));
    undo_->setShortcut(QKeySequence::Undo);
    redo_ = new QAction(tr("&Redo"), this);
    redo_->setObjectName(QStringLiteral("redoAction"));
    redo_->setIcon(workbenchIcon("redo"));
    redo_->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    connect(undo_, &QAction::triggered, this, [this] { step(true); });
    connect(redo_, &QAction::triggered, this, [this] { step(false); });
    auto* document = editor_->document();
    connect(document, &QTextDocument::undoAvailable, this, [this] { updateActions(); });
    connect(document, &QTextDocument::redoAvailable, this, [this] { updateActions(); });
    connect(document, &QTextDocument::undoCommandAdded, this, [this] { record(); });
    qApp->installEventFilter(this);
    updateActions();
}

SourceHistory::~SourceHistory()
{
    qApp->removeEventFilter(this);
}

void SourceHistory::replaceSource(const Entry& entry, const QString& source)
{
    pending_ = entry;
    editor_->replaceTextAsSingleEdit(source);
    pending_.reset();
}

void SourceHistory::clear()
{
    pending_.reset();
    done_.clear();
    undone_.clear();
    updateActions();
}

void SourceHistory::setStepCallback(std::function<void(const Entry&, bool)> callback)
{
    stepped_ = std::move(callback);
}

void SourceHistory::setRevertTypingCallback(std::function<bool()> callback)
{
    revertTyping_ = std::move(callback);
}

auto SourceHistory::eventFilter(QObject* watched, QEvent* event) -> bool
{
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress) return false;
    auto* key = static_cast<QKeyEvent*>(event);
    const auto isUndo = key->matches(QKeySequence::Undo);
    const auto isRedo = key->matches(QKeySequence::Redo)
        || key->keyCombination() == QKeyCombination(Qt::CTRL, Qt::Key_Y)
        || key->keyCombination() == QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_Z);
    auto* widget = qobject_cast<QWidget*>(watched);
    if ((!isUndo && !isRedo) || widget == nullptr || widget->window() != window_
        || editor_->isAncestorOf(widget) || widget == editor_) {
        return false;
    }
    // The key press after a Ctrl+Z that put back a typed value: the field must not undo too.
    if (event->type() == QEvent::KeyPress) return true;
    // A text field would take these keys for its own typing; here they belong to the model,
    // after a value typed but not yet applied is put back.
    if (isUndo && revertTyping_ && revertTyping_()) {
        key->accept();
        return true;
    }
    key->ignore();
    return true;
}

void SourceHistory::step(bool undo)
{
    auto* document = editor_->document();
    if (undo ? !document->isUndoAvailable() : !document->isRedoAvailable()) return;
    auto& from = undo ? done_ : undone_;
    auto& to = undo ? undone_ : done_;
    auto entry = from.empty() ? Entry{tr("Raw Source Edit")} : from.back();
    if (!from.empty()) from.pop_back();
    stepping_ = true;
    if (undo) editor_->undo();
    else editor_->redo();
    stepping_ = false;
    to.push_back(entry);
    updateActions();
    if (stepped_) stepped_(entry, undo);
}

void SourceHistory::record()
{
    if (stepping_) return;
    done_.push_back(pending_.value_or(Entry{tr("Raw Source Edit")}));
    undone_.clear();
    updateActions();
}

void SourceHistory::updateActions()
{
    const auto* document = editor_->document();
    const auto describe = [](QAction* action, bool available, const std::vector<Entry>& entries,
                              const QString& verb, const QString& menuVerb) {
        action->setEnabled(available);
        const auto what = available && !entries.empty() ? entries.back().description : QString{};
        action->setText(what.isEmpty() ? menuVerb : tr("%1 %2").arg(menuVerb, what));
        action->setToolTip(what.isEmpty() ? verb : tr("%1: %2").arg(verb, what));
    };
    describe(undo_, document->isUndoAvailable(), done_, tr("Undo"), tr("&Undo"));
    describe(redo_, document->isRedoAvailable(), undone_, tr("Redo"), tr("&Redo"));
}

}

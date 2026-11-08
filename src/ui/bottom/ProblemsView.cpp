#include "ui/bottom/ProblemsView.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace necwb::ui {
namespace {

constexpr auto SourceLineRole = Qt::UserRole + 1;

auto toolButton(const QString& text, QWidget* parent) -> QToolButton*
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setAutoRaise(true);
    return button;
}

}

ProblemsView::ProblemsView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("problemsView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* header = new QHBoxLayout;
    header->setContentsMargins(4, 0, 0, 0);
    header->setSpacing(0);
    summary_ = new QLabel(tr("Run Check Model to evaluate the current source."), this);
    summary_->setObjectName(QStringLiteral("problemsSummary"));
    summary_->setMinimumWidth(0);
    summary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* recheck = toolButton(tr("Recheck"), this);
    auto* generatedDeck = toolButton(tr("Deck…"), this);
    generatedDeck->setToolTip(tr("View the numeric solver deck generated from this source"));
    auto* changeSolver = toolButton(tr("Solver…"), this);
    changeSolver->setToolTip(tr("Change the solver backend this model is checked against"));
    header->addWidget(summary_, 1);
    header->addWidget(recheck);
    header->addWidget(generatedDeck);
    header->addWidget(changeSolver);
    findings_ = new QTreeWidget(this);
    findings_->setObjectName(QStringLiteral("problemsList"));
    findings_->setHeaderLabels({tr("Severity"), tr("Where"), tr("Message")});
    findings_->setRootIsDecorated(false);
    findings_->setUniformRowHeights(true);
    findings_->setAlternatingRowColors(true);
    findings_->setFrameShape(QFrame::NoFrame);
    findings_->header()->setStretchLastSection(true);
    findings_->header()->setSectionResizeMode(QHeaderView::Interactive);
    findings_->setColumnWidth(0, 72);
    findings_->setColumnWidth(1, 72);
    findings_->setToolTip(tr("Double-click a finding to select its wire or card"));
    layout->addLayout(header);
    layout->addWidget(findings_, 1);

    connect(findings_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const auto line = item->data(0, SourceLineRole).toULongLong();
        if (line != 0) emit findingActivated(static_cast<std::size_t>(line));
    });
    connect(recheck, &QToolButton::clicked, this, &ProblemsView::recheckRequested);
    connect(generatedDeck, &QToolButton::clicked, this, &ProblemsView::generatedDeckRequested);
    connect(changeSolver, &QToolButton::clicked, this, &ProblemsView::changeSolverRequested);
}

void ProblemsView::setFindings(std::span<const nec::ModelDiagnostic> diagnostics,
    const model::AntennaModel& model, const QString& summary)
{
    summary_->setText(summary);
    summary_->setToolTip(summary);
    findings_->clear();
    for (const auto& diagnostic : diagnostics) {
        const auto isError = diagnostic.severity == nec::DiagnosticSeverity::Error;
        const auto isWarning = diagnostic.severity == nec::DiagnosticSeverity::Warning;
        QString where;
        if (diagnostic.lineNumber != 0) {
            where = tr("L%1").arg(diagnostic.lineNumber);
            const auto& wires = model.wires();
            const auto wire = std::ranges::find(wires, diagnostic.lineNumber, &model::Wire::sourceLine);
            if (wire != wires.end()) where = tr("W%1 · %2").arg(wire->tag).arg(where);
        }
        auto* item = new QTreeWidgetItem(findings_, {
            isError ? tr("Error") : isWarning ? tr("Warning") : tr("Converted"),
            where, QString::fromStdString(diagnostic.message)});
        item->setData(0, SourceLineRole, static_cast<qulonglong>(diagnostic.lineNumber));
        item->setIcon(0, style()->standardIcon(isError ? QStyle::SP_MessageBoxCritical
            : isWarning ? QStyle::SP_MessageBoxWarning : QStyle::SP_MessageBoxInformation));
        item->setToolTip(2, QString::fromStdString(diagnostic.category));
    }
}

void ProblemsView::clearFindings(const QString& summary)
{
    summary_->setText(summary);
    summary_->setToolTip(summary);
    findings_->clear();
}

}

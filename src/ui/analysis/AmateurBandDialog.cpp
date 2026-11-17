#include "ui/analysis/AmateurBandDialog.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace necwb::ui {

auto chooseAmateurBands(QWidget* parent, const AmateurBandChoice& choice)
    -> std::vector<std::size_t>
{
    const auto& presets = analysis::amateurBandPresets();
    QDialog dialog(parent);
    dialog.setObjectName(choice.objectName);
    dialog.setWindowTitle(choice.title);
    auto* layout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(choice.description, &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* bandLayout = new QGridLayout;
    std::vector<QCheckBox*> checks;
    checks.reserve(presets.size());
    for (std::size_t index = 0; index < presets.size(); ++index) {
        auto* check = new QCheckBox(choice.label(presets[index]), &dialog);
        check->setObjectName(choice.checkObjectName + QString::number(index));
        checks.push_back(check);
        bandLayout->addWidget(check, static_cast<int>(index % 6), static_cast<int>(index / 6));
    }
    layout->addLayout(bandLayout);

    auto* selectionRow = new QHBoxLayout;
    auto* selectAll = new QPushButton(QObject::tr("Select All"), &dialog);
    auto* clearSelection = new QPushButton(QObject::tr("Clear"), &dialog);
    auto* summary = new QLabel(&dialog);
    summary->setObjectName(choice.summaryObjectName);
    selectionRow->addWidget(selectAll);
    selectionRow->addWidget(clearSelection);
    selectionRow->addStretch();
    selectionRow->addWidget(summary);
    layout->addLayout(selectionRow);

    auto* dialogButtons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* addSelected = dialogButtons->addButton(choice.acceptText, QDialogButtonBox::AcceptRole);
    addSelected->setObjectName(choice.acceptObjectName);
    layout->addWidget(dialogButtons);

    const auto ticked = [&checks] {
        std::vector<std::size_t> indexes;
        for (std::size_t index = 0; index < checks.size(); ++index)
            if (checks[index]->isChecked()) indexes.push_back(index);
        return indexes;
    };
    const auto updateSummary = [&] {
        const auto indexes = ticked();
        summary->setText(choice.summary(indexes));
        addSelected->setEnabled(!indexes.empty());
    };
    for (auto* check : checks)
        QObject::connect(check, &QCheckBox::toggled, &dialog, updateSummary);
    QObject::connect(selectAll, &QPushButton::clicked, &dialog, [&checks] {
        for (auto* check : checks) check->setChecked(true);
    });
    QObject::connect(clearSelection, &QPushButton::clicked, &dialog, [&checks] {
        for (auto* check : checks) check->setChecked(false);
    });
    QObject::connect(dialogButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(dialogButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updateSummary();
    if (dialog.exec() != QDialog::Accepted) return {};
    return ticked();
}

}

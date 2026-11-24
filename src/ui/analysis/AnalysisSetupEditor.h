#pragma once

#include <QHash>
#include <QWidget>

#include <utility>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace necwb::ui {

class AnalysisSetupEditor final : public QWidget {
    Q_OBJECT

public:
    explicit AnalysisSetupEditor(QWidget* parent = nullptr);

    // Display name and id of each engine Workbench can run.
    [[nodiscard]] static auto backends() -> std::vector<std::pair<QString, QString>>;
    // The paths are the ones the user chose; an engine without one uses the one found.
    void setSettings(const QString& backendId,
        const QHash<QString, QString>& executablePaths, int timeoutSeconds);
    // Switches engine as if chosen here, reporting the change.
    void selectBackend(const QString& backendId);

signals:
    // The executable path is the one the user chose, empty when none.
    void settingsChanged(QString backendId, QString executablePath, int timeoutSeconds);
    // Only when the user types or browses to a path, never on switching engines.
    void executablePathChanged(QString backendId, QString executablePath);

private:
    void choosePath(const QString& path);
    void updateDescription();
    void emitSettings();

    QComboBox* backendControl_{};
    QLineEdit* executableControl_{};
    QSpinBox* timeoutControl_{};
    QLabel* status_{};
    QHash<QString, QString> executablePaths_;
    QString selectedBackendId_;
    bool updating_{};
};

}

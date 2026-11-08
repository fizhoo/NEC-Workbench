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
    void setSettings(const QString& backendId,
        const QHash<QString, QString>& executablePaths, int timeoutSeconds);
    // Switches engine as if chosen here, reporting the change.
    void selectBackend(const QString& backendId);

signals:
    void settingsChanged(QString backendId, QString executablePath, int timeoutSeconds);
    void executablePathChanged(QString backendId, QString executablePath);

private:
    void updateDescription();
    void emitSettings();

    QComboBox* backendControl_{};
    QLineEdit* executableControl_{};
    QSpinBox* timeoutControl_{};
    QLabel* description_{};
    QLabel* status_{};
    QHash<QString, QString> executablePaths_;
    QString selectedBackendId_;
    bool updating_{};
};

}

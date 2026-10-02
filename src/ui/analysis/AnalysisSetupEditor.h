#pragma once

#include <QHash>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace necwb::ui {

class AnalysisSetupEditor final : public QWidget {
    Q_OBJECT

public:
    explicit AnalysisSetupEditor(QWidget* parent = nullptr);

    void setSettings(const QString& backendId,
        const QHash<QString, QString>& executablePaths, int timeoutSeconds);

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

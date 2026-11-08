#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QToolButton;

namespace necwb::ui {

// Live solver log: activity line, busy indicator, and the command/progress/diagnostic text.
class RunLogView final : public QWidget {
    Q_OBJECT

public:
    explicit RunLogView(QWidget* parent = nullptr);

    void beginRun(const QString& context, const QString& command, const QString& runDirectory);
    void append(const QString& text);
    void setActivity(const QString& activity);
    void setStopEnabled(bool enabled);
    void finishRun(bool completed);
    // Shows an archived run's log in place of the live one.
    void showArchivedLog(const QString& text);

signals:
    void stopRequested();

private:
    QLabel* activity_{};
    QProgressBar* progress_{};
    QToolButton* stop_{};
    QPlainTextEdit* log_{};
    QString command_;
    QString runDirectory_;
};

}

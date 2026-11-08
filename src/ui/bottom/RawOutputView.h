#pragma once

#include <QWidget>

#include <vector>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;

namespace necwb::ui {

// The complete, unparsed model.out of the loaded run, with frequency jump and text search.
class RawOutputView final : public QWidget {
    Q_OBJECT

public:
    explicit RawOutputView(QWidget* parent = nullptr);

    void setText(const QString& text);
    void setFrequencies(const std::vector<double>& frequenciesMHz);
    void clear();

signals:
    void statusMessage(const QString& message, int timeoutMs);

private:
    void jumpToSelectedFrequency();
    void findNext();

    QComboBox* frequency_{};
    QLineEdit* find_{};
    QPlainTextEdit* output_{};
};

}

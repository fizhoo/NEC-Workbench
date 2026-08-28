#pragma once

#include <QWidget>

class QAction;

namespace necwb::ui {

class WelcomePage final : public QWidget {
public:
    WelcomePage(QAction* newModelAction, QAction* openAction, QAction* geometryAction,
        QAction* sourceAction, QAction* analysisAction, QAction* resultsAction, QAction* optimizeAction,
        QWidget* parent = nullptr);
};

}

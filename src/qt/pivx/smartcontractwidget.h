// Copyright (c) 2026 The DSW developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCONTRACTWIDGET_H
#define SMARTCONTRACTWIDGET_H

#include "qt/pivx/pwidget.h"
#include <QWidget>

namespace Ui {
class SmartContractWidget;
}

class SmartContractWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SmartContractWidget(PIVXGUI* parent);
    ~SmartContractWidget();

    void loadWalletModel() override;

private Q_SLOTS:
    void generateContract();
    void onSaveClicked();
    void onPublishClicked();

private:
    Ui::SmartContractWidget *ui;
    UniValue activeDoc;
    std::string activeName;

    void updatePreviews();
};

#endif // SMARTCONTRACTWIDGET_H

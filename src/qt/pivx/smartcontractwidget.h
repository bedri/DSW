// Copyright (c) 2026 The DSW developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SMARTCONTRACTWIDGET_H
#define SMARTCONTRACTWIDGET_H

#include "qt/pivx/pwidget.h"
#include "uint256.h"
#include "script/script.h"
#include <QWidget>
#include <univalue.h>

namespace Ui {
class SmartContractWidget;
}

// Forward declarations
class CWalletTx;

// Holds info about a scannable contract UTXO
struct ContractUtxo {
    uint256 txid;
    int vout;
    CAmount amount;
    int depth;          // confirmations
    CScript scriptPubKey;
    UniValue decompiled; // result of CMescal::Decompile
};

class SmartContractWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SmartContractWidget(PIVXGUI* parent);
    ~SmartContractWidget();

    void loadWalletModel() override;

private Q_SLOTS:
    // Build tab
    void generateContract();
    void onSaveClicked();
    void onPublishClicked();
    void onTemplateSelected(int index);

    // Run tab
    void onRefreshContractsClicked();
    void onContractSelected(int index);
    void onRunContractClicked();

    // Custom Builder / Developer Mode slots
    void onAddActionClicked();
    void onRemoveActionClicked();
    void onMoveActionUp();
    void onMoveActionDown();
    void onOpenVisualDesignerClicked();
    void onDeveloperModeToggled(bool checked);
    void onYAMLTextChanged();

private:
    Ui::SmartContractWidget *ui;

    // Build tab state
    UniValue activeDoc;
    std::string activeName;
    void updatePreviews();

    // Custom Builder state
    UniValue customActions;
    void updateCustomTree();
    void buildContractFromCustom();

    // Run tab state
    QList<ContractUtxo> m_utxos;
    void populateRunDetails(const ContractUtxo& utxo);
    void clearRunDetails();
    void detectAutoSignCapabilities(const ContractUtxo& utxo);
};

#endif // SMARTCONTRACTWIDGET_H

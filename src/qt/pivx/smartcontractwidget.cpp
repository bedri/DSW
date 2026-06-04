// Copyright (c) 2026 The DSW developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/pivx/smartcontractwidget.h"
#include "qt/pivx/forms/ui_smartcontractwidget.h"
#include "qt/pivx/pivxgui.h"
#include "qt/pivx/qtutils.h"
#include "guiutil.h"
#include "walletmodel.h"
#include "wallet/wallet.h"
#include "script/mescal.h"
#include "script/standard.h"
#include "net.h"
#include "main.h"       // cs_main, COIN
#include "coincontrol.h"
#include "utilstrencodings.h"
#include "primitives/transaction.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QFile>

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string UniValueToYAML(const UniValue& val, int indent = 0) {
    std::string result;
    std::string spaces(indent, ' ');
    if (val.isObject()) {
        const std::vector<std::string>& keys = val.getKeys();
        for (const std::string& key : keys) {
            const UniValue& subVal = val[key];
            if (subVal.isObject()) {
                result += spaces + key + ":\n" + UniValueToYAML(subVal, indent + 2);
            } else if (subVal.isArray()) {
                result += spaces + key + ":\n" + UniValueToYAML(subVal, indent + 2);
            } else {
                result += spaces + key + ": " + UniValueToYAML(subVal, 0);
            }
        }
    } else if (val.isArray()) {
        for (unsigned int i = 0; i < val.size(); ++i) {
            const UniValue& subVal = val[i];
            if (subVal.isObject() || subVal.isArray()) {
                result += spaces + "- \n" + UniValueToYAML(subVal, indent + 2);
            } else {
                result += spaces + "- " + UniValueToYAML(subVal, 0);
            }
        }
    } else if (val.isNum()) {
        result += val.getValStr() + "\n";
    } else if (val.isBool()) {
        result += std::string(val.get_bool() ? "true" : "false") + "\n";
    } else {
        result += "\"" + val.get_str() + "\"\n";
    }
    return result;
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────

SmartContractWidget::SmartContractWidget(PIVXGUI* parent) :
    PWidget(parent),
    ui(new Ui::SmartContractWidget)
{
    ui->setupUi(this);

    this->setStyleSheet(parent->styleSheet());

    // ── Build tab ─────────────────────────────────────────────────────────────
    setCssProperty(ui->left, "container");
    ui->left->setContentsMargins(20, 20, 20, 20);
    setCssProperty(ui->right, "container-right");
    ui->right->setContentsMargins(20, 20, 20, 20);

    setCssProperty(ui->labelTitle, "text-title-screen");
    setCssProperty(ui->labelSubtitle1, "text-subtitle");
    setCssProperty(ui->labelTitleRight, "text-title-screen");
    setCssProperty(ui->lblYAML, "text-title");
    setCssProperty(ui->lblJSON, "text-title");

    setCssProperty({ui->lblTLName, ui->lblTLExpiry, ui->lblTLPubkey,
                    ui->lblMSName, ui->lblMSN, ui->lblMSM, ui->lblMSKeys,
                    ui->lblHLName, ui->lblHLHash, ui->lblHLPubkey,
                    ui->labelAmount}, "text-title");

    for (QLineEdit* le : {ui->lineEditNameTimeLock, ui->lineEditExpiryTimeLock, ui->lineEditPubkeyTimeLock,
                           ui->lineEditNameMultiSig, ui->lineEditNMultiSig, ui->lineEditMMultiSig,
                           ui->lineEditNameHashLock, ui->lineEditHashHashLock, ui->lineEditPubkeyHashLock,
                           ui->lineEditAmount}) {
        setCssEditLine(le, true);
    }

    setCssBtnSecondary(ui->btnSave);
    setCssBtnPrimary(ui->btnPublish);

    // Live preview connections
    connect(ui->lineEditNameTimeLock,   &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditExpiryTimeLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditPubkeyTimeLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditNameMultiSig,   &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditNMultiSig,      &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditMMultiSig,      &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->plainTextEditKeysMultiSig, &QPlainTextEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditNameHashLock,   &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditHashHashLock,   &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditPubkeyHashLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &SmartContractWidget::generateContract);

    connect(ui->btnSave,    &QPushButton::clicked, this, &SmartContractWidget::onSaveClicked);
    connect(ui->btnPublish, &QPushButton::clicked, this, &SmartContractWidget::onPublishClicked);

    // ── Run tab ───────────────────────────────────────────────────────────────
    setCssProperty(ui->runLeft, "container");
    ui->runLeft->setContentsMargins(20, 20, 20, 20);
    setCssProperty(ui->runRight, "container-right");
    ui->runRight->setContentsMargins(20, 20, 20, 20);

    setCssProperty(ui->labelRunTitle,    "text-title-screen");
    setCssProperty(ui->labelRunSubtitle, "text-subtitle");
    setCssProperty(ui->lblSelectContract,"text-title");
    setCssProperty(ui->lblRecipAddr,     "text-title");
    setCssProperty(ui->lblUnlockParams,  "text-title-screen");
    setCssProperty({ui->lblParam1, ui->lblParam2, ui->lblParam3}, "text-title");
    setCssProperty(ui->labelRunRightTitle, "text-title-screen");
    setCssProperty(ui->lblDecompiledTitle, "text-title");
    setCssProperty(ui->lblRawScript,       "text-title");

    // UTXO info labels
    setCssProperty({ui->lblUtxoTxId, ui->lblUtxoVout,
                    ui->lblUtxoAmount, ui->lblUtxoConfs}, "text-title");

    setCssEditLine(ui->lineEditRecipAddr, true);
    setCssEditLine(ui->lineEditParam1,    true);
    setCssEditLine(ui->lineEditParam2,    true);
    setCssEditLine(ui->lineEditRawScript, true);

    setCssBtnSecondary(ui->btnRefreshContracts);
    setCssBtnPrimary(ui->btnRunContract);

    connect(ui->btnRefreshContracts, &QPushButton::clicked, this, &SmartContractWidget::onRefreshContractsClicked);
    connect(ui->comboContracts, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SmartContractWidget::onContractSelected);
    connect(ui->btnRunContract, &QPushButton::clicked, this, &SmartContractWidget::onRunContractClicked);
    connect(ui->tabWidgetMode, &QTabWidget::currentChanged, this, [this](int index){
        if (index == 1) {
            onRefreshContractsClicked();
        }
    });

    // ── Custom tab styling & connections ──────────────────────────────────────
    setCssBtnSecondary(ui->btnAddAction);
    setCssBtnSecondary(ui->btnRemoveAction);
    setCssBtnSecondary(ui->btnMoveActionUp);
    setCssBtnSecondary(ui->btnMoveActionDown);
    setCssBtnPrimary(ui->btnOpenVisualDesigner);

    connect(ui->btnAddAction, &QPushButton::clicked, this, &SmartContractWidget::onAddActionClicked);
    connect(ui->btnRemoveAction, &QPushButton::clicked, this, &SmartContractWidget::onRemoveActionClicked);
    connect(ui->btnMoveActionUp, &QPushButton::clicked, this, &SmartContractWidget::onMoveActionUp);
    connect(ui->btnMoveActionDown, &QPushButton::clicked, this, &SmartContractWidget::onMoveActionDown);
    connect(ui->btnOpenVisualDesigner, &QPushButton::clicked, this, &SmartContractWidget::onOpenVisualDesignerClicked);
    connect(ui->cbDeveloperMode, &QCheckBox::toggled, this, &SmartContractWidget::onDeveloperModeToggled);
    connect(ui->textEditYAML, &QTextEdit::textChanged, this, &SmartContractWidget::onYAMLTextChanged);

    customActions = UniValue(UniValue::VARR);

    // Initial state
    generateContract();
    clearRunDetails();
}

SmartContractWidget::~SmartContractWidget()
{
    delete ui;
}

// ─── loadWalletModel ──────────────────────────────────────────────────────────

void SmartContractWidget::loadWalletModel()
{
    if (walletModel) {
        CPubKey pubKey;
        if (pwalletMain && pwalletMain->GetKeyFromPool(pubKey)) {
            std::string pubkeyHex = HexStr(pubKey.begin(), pubKey.end());
            ui->lineEditPubkeyTimeLock->setText(QString::fromStdString(pubkeyHex));
            ui->lineEditPubkeyHashLock->setText(QString::fromStdString(pubkeyHex));
        }
        generateContract();

        // Automatically refresh when wallet balance/transactions change
        connect(walletModel, &WalletModel::balanceChanged, this, &SmartContractWidget::onRefreshContractsClicked);
        onRefreshContractsClicked();
    }
}

// ═══════════════════════════════════════════════════════════════════
//  BUILD TAB
// ═══════════════════════════════════════════════════════════════════

void SmartContractWidget::generateContract()
{
    int tabIndex = ui->tabWidget->currentIndex();
    UniValue doc(UniValue::VOBJ);
    UniValue basic(UniValue::VOBJ);
    UniValue contract(UniValue::VOBJ);

    if (tabIndex == 0) { // Time-Locked
        std::string name     = ui->lineEditNameTimeLock->text().toStdString();
        std::string expiryStr= ui->lineEditExpiryTimeLock->text().toStdString();
        std::string pubkey   = ui->lineEditPubkeyTimeLock->text().toStdString();

        activeName = name;

        UniValue expiryVal;
        try { expiryVal = UniValue((int64_t)std::stoll(expiryStr)); }
        catch (...) { expiryVal = UniValue(expiryStr); }

        UniValue tlInputs(UniValue::VARR);
        UniValue tlInput1(UniValue::VOBJ);
        tlInput1.pushKV("name",  "Lock-Until");
        tlInput1.pushKV("type",  "timestamp-or-block-height");
        tlInput1.pushKV("value", expiryVal);
        tlInputs.push_back(tlInput1);

        UniValue tlSpec(UniValue::VOBJ);
        tlSpec.pushKV("role",   "lock-time");
        tlSpec.pushKV("inputs", tlInputs);
        basic.pushKV("Lock-Time", tlSpec);

        UniValue osInputs(UniValue::VARR);
        UniValue osInput1(UniValue::VOBJ);
        osInput1.pushKV("name",  "Pubkey");
        osInput1.pushKV("type",  "pubkey");
        osInput1.pushKV("value", pubkey);
        osInputs.push_back(osInput1);

        UniValue osSpec(UniValue::VOBJ);
        osSpec.pushKV("role",   "check-signature-verification");
        osSpec.pushKV("inputs", osInputs);
        basic.pushKV("Owner-Sig", osSpec);

        UniValue cSpec(UniValue::VOBJ);
        cSpec.pushKV("description", "Funds locked requiring owner signature.");
        UniValue actions(UniValue::VARR);
        UniValue action1(UniValue::VOBJ); action1.pushKV("type","basic"); action1.pushKV("name","Lock-Time");  actions.push_back(action1);
        UniValue action2(UniValue::VOBJ); action2.pushKV("type","basic"); action2.pushKV("name","Owner-Sig");  actions.push_back(action2);
        cSpec.pushKV("actions", actions);
        contract.pushKV(name, cSpec);

        doc.pushKV("basic",           basic);
        doc.pushKV("contract",        contract);
        doc.pushKV("active_contract", name);

    } else if (tabIndex == 1) { // Multi-Signature
        std::string name = ui->lineEditNameMultiSig->text().toStdString();
        int n = ui->lineEditNMultiSig->text().toInt();
        int m = ui->lineEditMMultiSig->text().toInt();
        QStringList keysList = ui->plainTextEditKeysMultiSig->toPlainText()
                                   .split('\n', QString::SkipEmptyParts);

        activeName = name;

        UniValue msInputs(UniValue::VARR);
        UniValue msInput1(UniValue::VOBJ); msInput1.pushKV("name","m"); msInput1.pushKV("type","number"); msInput1.pushKV("value",m); msInputs.push_back(msInput1);
        UniValue msInput2(UniValue::VOBJ); msInput2.pushKV("name","n"); msInput2.pushKV("type","number"); msInput2.pushKV("value",n); msInputs.push_back(msInput2);

        UniValue keysArray(UniValue::VARR);
        for (const QString& k : keysList)
            keysArray.push_back(k.trimmed().toStdString());

        UniValue msInput3(UniValue::VOBJ); msInput3.pushKV("name","Signatures"); msInput3.pushKV("type","array"); msInput3.pushKV("value",keysArray); msInputs.push_back(msInput3);

        UniValue msSpec(UniValue::VOBJ);
        msSpec.pushKV("role",   "multi-signature");
        msSpec.pushKV("inputs", msInputs);
        basic.pushKV("Multi-Signature", msSpec);

        UniValue cSpec(UniValue::VOBJ);
        cSpec.pushKV("description", std::to_string(m)+"-of-"+std::to_string(n)+" multi-signature smart contract.");
        UniValue actions(UniValue::VARR);
        UniValue action1(UniValue::VOBJ); action1.pushKV("type","basic"); action1.pushKV("name","Multi-Signature"); actions.push_back(action1);
        cSpec.pushKV("actions", actions);
        contract.pushKV(name, cSpec);

        doc.pushKV("basic",           basic);
        doc.pushKV("contract",        contract);
        doc.pushKV("active_contract", name);

    } else if (tabIndex == 2) { // Hash-Locked
        std::string name    = ui->lineEditNameHashLock->text().toStdString();
        std::string hash160 = ui->lineEditHashHashLock->text().toStdString();
        std::string pubkey  = ui->lineEditPubkeyHashLock->text().toStdString();

        activeName = name;

        UniValue hcInputs(UniValue::VARR);
        UniValue hcInput1(UniValue::VOBJ); hcInput1.pushKV("name","Hash160"); hcInput1.pushKV("type","string-or-number"); hcInput1.pushKV("value",hash160); hcInputs.push_back(hcInput1);

        UniValue hcSpec(UniValue::VOBJ);
        hcSpec.pushKV("role",   "hash160");
        hcSpec.pushKV("inputs", hcInputs);
        basic.pushKV("Hash-Check", hcSpec);

        UniValue rsInputs(UniValue::VARR);
        UniValue rsInput1(UniValue::VOBJ); rsInput1.pushKV("name","Pubkey"); rsInput1.pushKV("type","pubkey"); rsInput1.pushKV("value",pubkey); rsInputs.push_back(rsInput1);

        UniValue rsSpec(UniValue::VOBJ);
        rsSpec.pushKV("role",   "check-signature-verification");
        rsSpec.pushKV("inputs", rsInputs);
        basic.pushKV("Recipient-Sig", rsSpec);

        UniValue cSpec(UniValue::VOBJ);
        cSpec.pushKV("description", "Requires revealing preimage and recipient signature.");
        UniValue actions(UniValue::VARR);
        UniValue action1(UniValue::VOBJ); action1.pushKV("type","basic"); action1.pushKV("name","Hash-Check");     actions.push_back(action1);
        UniValue action2(UniValue::VOBJ); action2.pushKV("type","basic"); action2.pushKV("name","Recipient-Sig");  actions.push_back(action2);
        cSpec.pushKV("actions", actions);
        contract.pushKV(name, cSpec);

        doc.pushKV("basic",           basic);
        doc.pushKV("contract",        contract);
        doc.pushKV("active_contract", name);
    } else if (tabIndex == 3) { // Custom Builder
        buildContractFromCustom();
        return;
    }

    activeDoc = doc;
    updatePreviews();
}

void SmartContractWidget::updatePreviews()
{
    ui->textEditJSON->setText(QString::fromStdString(activeDoc.write(2)));
    ui->textEditYAML->setText(QString::fromStdString(UniValueToYAML(activeDoc, 0)));
}

void SmartContractWidget::onSaveClicked()
{
    if (activeName.empty()) {
        QMessageBox::critical(this, tr("Error"), tr("Contract name is empty."));
        return;
    }
    QString defaultFileName = QString::fromStdString(activeName) + ".json";
    QString path = QFileDialog::getSaveFileName(this, tr("Save Smart Contract"), defaultFileName, tr("JSON Files (*.json)"));
    if (path.isEmpty()) return;

    QFile jsonFile(path);
    if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&jsonFile);
        out << QString::fromStdString(activeDoc.write(2));
        jsonFile.close();
    }

    QString yamlPath = path;
    if (yamlPath.endsWith(".json")) { yamlPath.chop(5); yamlPath.append(".yaml"); }
    else { yamlPath.append(".yaml"); }

    QFile yamlFile(yamlPath);
    if (yamlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&yamlFile);
        out << QString::fromStdString(UniValueToYAML(activeDoc, 0));
        yamlFile.close();
    }

    inform(tr("Contract spec saved to:\n%1\n%2").arg(path).arg(yamlPath));
}

void SmartContractWidget::onPublishClicked()
{
    if (!walletModel) {
        inform(tr("Wallet model not loaded."));
        return;
    }

    // 1. Compile
    std::string jsonStr = activeDoc.write();
    std::string errorStr;
    CScript script = CMescal::Compile(jsonStr, errorStr);
    if (!errorStr.empty()) {
        QMessageBox::critical(this, tr("Compilation Error"),
            tr("MESCAL compilation failed:\n%1").arg(QString::fromStdString(errorStr)));
        return;
    }

    // 2. Validate Amount
    bool amountOk = false;
    double amountDouble = ui->lineEditAmount->text().toDouble(&amountOk);
    if (!amountOk || amountDouble < 0) {
        QMessageBox::critical(this, tr("Invalid Amount"), tr("Please enter a valid amount of DSW."));
        return;
    }
    CAmount amount = amountDouble * COIN;

    // 3. Unlock Wallet
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        inform(tr("Wallet unlock failed"));
        return;
    }

    // 4. Send transaction
    if (!pwalletMain) {
        inform(tr("Wallet database not initialized."));
        return;
    }

    std::vector<CRecipient> vecSend;
    vecSend.push_back(CRecipient{script, amount, false});

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired = 0;
    int nChangePosInOut = -1;
    std::string strFailReason;
    CCoinControl coinControl;

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        bool fCreated = pwalletMain->CreateTransaction(vecSend,
                                                      wtx,
                                                      reservekey,
                                                      nFeeRequired,
                                                      nChangePosInOut,
                                                      strFailReason,
                                                      &coinControl,
                                                      ALL_COINS,
                                                      true,
                                                      0);
        if (!fCreated) {
            inform(tr("Transaction creation failed: %1").arg(QString::fromStdString(strFailReason)));
            return;
        }
    }

    CWallet::CommitResult res = pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get());
    if (res.status != CWallet::CommitStatus::OK) {
        QMessageBox::critical(this, tr("Commit Failed"),
            tr("Transaction commit failed:\n%1").arg(QString::fromStdString(res.ToString())));
        return;
    }

    // 5. Notify UI
    walletModel->emitBalanceChanged();

    std::string txid = res.hashTx.GetHex();
    ui->labelStatus->setText(tr("Status: Published! TxID: %1").arg(QString::fromStdString(txid)));
    inform(tr("Contract published successfully!\nTxID: %1").arg(QString::fromStdString(txid)));
}

// ═══════════════════════════════════════════════════════════════════
//  RUN TAB
// ═══════════════════════════════════════════════════════════════════

void SmartContractWidget::onRefreshContractsClicked()
{
    if (!pwalletMain) {
        ui->labelRunStatus->setText(tr("Status: Wallet not loaded."));
        return;
    }

    m_utxos.clear();
    ui->comboContracts->clear();
    clearRunDetails();

    LOCK2(cs_main, pwalletMain->cs_wallet);

    for (const auto& kv : pwalletMain->mapWallet) {
        const CWalletTx& wtx = kv.second;
        int depth = wtx.GetDepthInMainChain();

        for (int i = 0; i < (int)wtx.vout.size(); i++) {
            const CTxOut& out = wtx.vout[i];

            // Skip if already spent
            if (pwalletMain->IsSpent(kv.first, i))
                continue;

            // Only consider non-standard scripts (MESCAL contracts won't be P2PKH/P2PK/P2SH)
            txnouttype scriptType;
            std::vector<std::vector<unsigned char>> vSolutions;
            if (Solver(out.scriptPubKey, scriptType, vSolutions)) {
                // Standard script (P2PKH, P2PK, P2SH, MULTISIG, NULL_DATA) — skip
                if (scriptType != TX_NONSTANDARD)
                    continue;
            }

            // Decompile and verify it produces valid MESCAL (non-empty actions)
            std::string decompileErr;
            UniValue decompiled = CMescal::Decompile(out.scriptPubKey, decompileErr);
            if (!decompileErr.empty())
                continue;

            // Must have at least one action to be a real MESCAL contract
            if (!decompiled.exists("actions"))
                continue;
            const UniValue& acts = decompiled["actions"];
            if (!acts.isArray() || acts.empty())
                continue;

            // Round-trip check: re-compile decompiled JSON and compare scripts
            std::string recompileErr;
            CScript recompiled = CMescal::Compile(decompiled.write(), recompileErr);
            if (!recompileErr.empty() || recompiled != out.scriptPubKey)
                continue;

            ContractUtxo utxo;
            utxo.txid         = kv.first;
            utxo.vout         = i;
            utxo.amount       = out.nValue;
            utxo.depth        = depth;
            utxo.scriptPubKey = out.scriptPubKey;
            utxo.decompiled   = decompiled;

            m_utxos.append(utxo);

            QString label = QString("%1...:%2  |  %3 DSW  (%4 confs)")
                .arg(QString::fromStdString(kv.first.GetHex()).left(8))
                .arg(i)
                .arg(out.nValue / (double)COIN, 0, 'f', 4)
                .arg(depth);

            ui->comboContracts->addItem(label);
        }
    }

    if (m_utxos.isEmpty()) {
        ui->labelRunStatus->setText(tr("Status: No unspent contract UTXOs found in wallet."));
    } else {
        ui->labelRunStatus->setText(tr("Status: Found %1 contract UTXO(s).").arg(m_utxos.size()));
        onContractSelected(0);
    }
}

void SmartContractWidget::onContractSelected(int index)
{
    if (index < 0 || index >= m_utxos.size()) {
        clearRunDetails();
        return;
    }
    populateRunDetails(m_utxos[index]);
}

void SmartContractWidget::populateRunDetails(const ContractUtxo& utxo)
{
    ui->labelUtxoTxId->setText(QString::fromStdString(utxo.txid.GetHex()));
    ui->labelUtxoVout->setText(QString::number(utxo.vout));
    ui->labelUtxoAmount->setText(tr("%1 DSW").arg(utxo.amount / (double)COIN, 0, 'f', 8));
    ui->labelUtxoConfs->setText(QString::number(utxo.depth));

    // Decompiled JSON
    ui->textEditDecompiled->setText(QString::fromStdString(utxo.decompiled.write(2)));

    // Raw script hex
    std::vector<unsigned char> scriptBytes(utxo.scriptPubKey.begin(), utxo.scriptPubKey.end());
    ui->lineEditRawScript->setText(QString::fromStdString(HexStr(scriptBytes)));

    detectAutoSignCapabilities(utxo);
}

void SmartContractWidget::clearRunDetails()
{
    ui->labelUtxoTxId->setText("—");
    ui->labelUtxoVout->setText("—");
    ui->labelUtxoAmount->setText("—");
    ui->labelUtxoConfs->setText("—");
    ui->textEditDecompiled->clear();
    ui->lineEditRawScript->clear();

    ui->lblAutoSignStatus->setText("");
    ui->lblAutoSignStatus->setStyleSheet("");
    ui->lineEditParam1->setEnabled(true);
    ui->lineEditParam1->clear();
    ui->lineEditParam1->setPlaceholderText(tr("DER-encoded signature or preimage (hex)"));
    ui->lineEditParam2->setEnabled(true);
    ui->lineEditParam2->clear();
    ui->lineEditParam2->setPlaceholderText(tr("Compressed public key (hex, for sig-check contracts)"));
    ui->plainTextExtraSigs->setEnabled(true);
    ui->plainTextExtraSigs->clear();
    ui->plainTextExtraSigs->setPlaceholderText(tr("Additional DER signatures for multi-sig (one hex per line)"));
}

void SmartContractWidget::detectAutoSignCapabilities(const ContractUtxo& utxo)
{
    // Reset status and inputs
    ui->lblAutoSignStatus->setStyleSheet("");
    ui->lblAutoSignStatus->setText("");
    ui->lineEditParam1->setEnabled(true);
    ui->lineEditParam1->clear();
    ui->lineEditParam1->setPlaceholderText(tr("DER-encoded signature or preimage (hex)"));
    ui->lineEditParam2->setEnabled(true);
    ui->lineEditParam2->clear();
    ui->lineEditParam2->setPlaceholderText(tr("Compressed public key (hex, for sig-check contracts)"));
    ui->plainTextExtraSigs->setEnabled(true);
    ui->plainTextExtraSigs->clear();

    if (!pwalletMain) return;

    // Detect contract type from decompiled
    bool isTimeLock = false;
    bool isMultiSig = false;
    bool isHashLock = false;

    std::string timeLockOwnerPubkey = "";
    std::vector<std::string> multiSigPubkeys;
    int multiSigM = 0;
    int multiSigN = 0;
    std::string hashLockPubkey = "";

    if (utxo.decompiled.exists("actions")) {
        const UniValue& actions = utxo.decompiled["actions"];
        if (actions.isArray()) {
            for (unsigned int i = 0; i < actions.size(); i++) {
                const UniValue& act = actions[i];
                if (!act.isObject()) continue;
                std::string role = act.exists("role") ? act["role"].get_str() : "";
                UniValue inputs = act.exists("inputs") ? act["inputs"] : UniValue();
                if (role == "lock-time") {
                    isTimeLock = true;
                } else if (role == "check-signature-verification") {
                    if (inputs.isArray() && !inputs.empty()) {
                        std::string pk = inputs[0]["value"].get_str();
                        if (isHashLock) hashLockPubkey = pk;
                        else timeLockOwnerPubkey = pk;
                    }
                } else if (role == "multi-signature") {
                    isMultiSig = true;
                    if (inputs.isArray()) {
                        for (unsigned int j = 0; j < inputs.size(); j++) {
                            std::string inputName = inputs[j]["name"].get_str();
                            if (inputName == "m") {
                                multiSigM = inputs[j]["value"].get_int();
                            } else if (inputName == "n") {
                                multiSigN = inputs[j]["value"].get_int();
                            } else if (inputName == "Signatures") {
                                const UniValue& sigsVal = inputs[j]["value"];
                                if (sigsVal.isArray()) {
                                    for (unsigned int k = 0; k < sigsVal.size(); k++) {
                                        multiSigPubkeys.push_back(sigsVal[k].get_str());
                                    }
                                }
                            }
                        }
                    }
                } else if (role == "hash160") {
                    isHashLock = true;
                }
            }
        }
    }

    LOCK(pwalletMain->cs_wallet);

    if (isTimeLock && !timeLockOwnerPubkey.empty()) {
        CPubKey pubKey(ParseHex(timeLockOwnerPubkey));
        if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
            ui->lblAutoSignStatus->setStyleSheet("color: #26a69a; font-weight: bold;");
            ui->lblAutoSignStatus->setText(tr("✓ Gerekli imza anahtarı cüzdanınızda bulundu. İşlem otomatik olarak imzalanacaktır."));
            
            // Disable manual parameters since they are not needed
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam1->setPlaceholderText(tr("(Cüzdan tarafından otomatik imzalanacak)"));
            ui->lineEditParam2->setText(QString::fromStdString(timeLockOwnerPubkey));
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setEnabled(false);
        } else {
            ui->lblAutoSignStatus->setStyleSheet("color: #ff9100;");
            ui->lblAutoSignStatus->setText(tr("⚠ Harcama anahtarı bu cüzdanda değil. İmza parametrelerini manuel girmelisiniz."));
        }
    } else if (isMultiSig) {
        int ownedKeys = 0;
        for (const auto& pkHex : multiSigPubkeys) {
            CPubKey pubKey(ParseHex(pkHex));
            if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                ownedKeys++;
            }
        }

        if (ownedKeys >= multiSigM) {
            ui->lblAutoSignStatus->setStyleSheet("color: #26a69a; font-weight: bold;");
            ui->lblAutoSignStatus->setText(tr("✓ Cüzdanda %1/%2 imza anahtarı bulundu (%3 imza yeterli). İşlem otomatik imzalanacaktır.").arg(ownedKeys).arg(multiSigN).arg(multiSigM));
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setEnabled(false);
        } else if (ownedKeys > 0) {
            ui->lblAutoSignStatus->setStyleSheet("color: #ff9100;");
            ui->lblAutoSignStatus->setText(tr("⚠ Cüzdanda %1/%2 imza anahtarı bulundu. Eksik %3 imza için lütfen diğer imzaları aşağıya girin.").arg(ownedKeys).arg(multiSigN).arg(multiSigM - ownedKeys));
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setPlaceholderText(tr("Lütfen eksik %1 imzayı buraya (satır başına bir tane) girin").arg(multiSigM - ownedKeys));
        } else {
            ui->lblAutoSignStatus->setStyleSheet("color: #ff3d00;");
            ui->lblAutoSignStatus->setText(tr("✗ Cüzdanda hiç imza anahtarı bulunamadı. Lütfen tüm %1 imzayı manuel girin.").arg(multiSigM));
        }
    } else if (isHashLock) {
        ui->lineEditParam1->setPlaceholderText(tr("Şifre Çözücü Metin (Preimage) girin"));
        ui->lineEditParam2->setEnabled(false);
        if (!hashLockPubkey.empty()) {
            CPubKey pubKey(ParseHex(hashLockPubkey));
            if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                ui->lblAutoSignStatus->setStyleSheet("color: #26a69a; font-weight: bold;");
                ui->lblAutoSignStatus->setText(tr("✓ Alıcı imza anahtarı cüzdanda bulundu. Sadece Şifre Çözücü Metin (Preimage) girmelisiniz."));
                ui->lineEditParam2->setText(QString::fromStdString(hashLockPubkey));
            } else {
                ui->lblAutoSignStatus->setStyleSheet("color: #ff9100;");
                ui->lblAutoSignStatus->setText(tr("⚠ Alıcı imza anahtarı cüzdanda değil. Şifre Çözücü Metin ve manuel Alıcı İmzası girmelisiniz."));
                ui->lineEditParam2->setEnabled(true);
            }
        }
    }
}

void SmartContractWidget::onRunContractClicked()
{
    int idx = ui->comboContracts->currentIndex();
    if (idx < 0 || idx >= m_utxos.size()) {
        ui->labelRunStatus->setText(tr("Status: No contract selected."));
        return;
    }
    if (!walletModel || !pwalletMain) {
        ui->labelRunStatus->setText(tr("Status: Wallet not loaded."));
        return;
    }

    const ContractUtxo& utxo = m_utxos[idx];

    // ── Validate recipient ────────────────────────────────────────────────────
    QString recipAddrStr = ui->lineEditRecipAddr->text().trimmed();
    if (recipAddrStr.isEmpty()) {
        QMessageBox::critical(this, tr("Missing Input"), tr("Please enter a recipient address."));
        return;
    }
    CTxDestination dest = DecodeDestination(recipAddrStr.toStdString());
    if (!IsValidDestination(dest)) {
        QMessageBox::critical(this, tr("Invalid Address"), tr("The recipient address is not valid."));
        return;
    }
    CScript recipScript = GetScriptForDestination(dest);

    // ── Build spending transaction ────────────────────────────────────────────
    CMutableTransaction rawTx;
    rawTx.nVersion = 1;

    // Input: spend the contract UTXO (start with empty scriptSig)
    CTxIn txIn(COutPoint(utxo.txid, utxo.vout));
    // For time-locked contracts, set nSequence to allow CLTV
    txIn.nSequence = std::numeric_limits<uint32_t>::max() - 1;
    rawTx.vin.push_back(txIn);

    // Calculate fee (estimate: 10000 satoshis for standard regtest/mainnet)
    CAmount feeEstimate = 10000;
    CAmount sendAmount = utxo.amount - feeEstimate;
    if (sendAmount <= 0) {
        QMessageBox::critical(this, tr("Error"), tr("UTXO amount too small to cover fee."));
        return;
    }

    // Output: recipient
    rawTx.vout.push_back(CTxOut(sendAmount, recipScript));

    // Detect contract type from decompiled for locktime setting
    bool isTimeLock = false;
    bool isMultiSig = false;
    bool isHashLock = false;
    int64_t lockTimeVal = 0;
    std::string timeLockOwnerPubkey = "";
    std::vector<std::string> multiSigPubkeys;
    int multiSigM = 0;
    int multiSigN = 0;
    std::string hashLockPubkey = "";

    if (utxo.decompiled.exists("actions")) {
        const UniValue& actions = utxo.decompiled["actions"];
        if (actions.isArray()) {
            for (unsigned int i = 0; i < actions.size(); i++) {
                const UniValue& act = actions[i];
                if (!act.isObject()) continue;
                std::string role = act.exists("role") ? act["role"].get_str() : "";
                UniValue inputs = act.exists("inputs") ? act["inputs"] : UniValue();
                if (role == "lock-time") {
                    isTimeLock = true;
                    if (inputs.isArray() && !inputs.empty()) {
                        lockTimeVal = inputs[0]["value"].get_int64();
                    }
                } else if (role == "check-signature-verification") {
                    if (inputs.isArray() && !inputs.empty()) {
                        std::string pk = inputs[0]["value"].get_str();
                        if (isHashLock) hashLockPubkey = pk;
                        else timeLockOwnerPubkey = pk;
                    }
                } else if (role == "multi-signature") {
                    isMultiSig = true;
                    if (inputs.isArray()) {
                        for (unsigned int j = 0; j < inputs.size(); j++) {
                            std::string inputName = inputs[j]["name"].get_str();
                            if (inputName == "m") {
                                multiSigM = inputs[j]["value"].get_int();
                            } else if (inputName == "n") {
                                multiSigN = inputs[j]["value"].get_int();
                            } else if (inputName == "Signatures") {
                                const UniValue& sigsVal = inputs[j]["value"];
                                if (sigsVal.isArray()) {
                                    for (unsigned int k = 0; k < sigsVal.size(); k++) {
                                        multiSigPubkeys.push_back(sigsVal[k].get_str());
                                    }
                                }
                            }
                        }
                    }
                } else if (role == "hash160") {
                    isHashLock = true;
                }
            }
        }
    }

    if (isTimeLock && lockTimeVal > 0) {
        rawTx.nLockTime = (uint32_t)lockTimeVal;
    }

    // ── Unlock Wallet ─────────────────────────────────────────────────────────
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        ui->labelRunStatus->setText(tr("Status: Wallet unlock failed."));
        return;
    }

    // ── Build scriptSig (unlock) ──────────────────────────────────────────────
    CScript scriptSig;

    // Check if auto-signing is possible
    bool useAutoSign = false;
    if (isTimeLock && !timeLockOwnerPubkey.empty()) {
        CPubKey pubKey(ParseHex(timeLockOwnerPubkey));
        if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
            useAutoSign = true;
        }
    } else if (isMultiSig) {
        int ownedKeys = 0;
        for (const auto& pkHex : multiSigPubkeys) {
            CPubKey pubKey(ParseHex(pkHex));
            if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                ownedKeys++;
            }
        }
        if (ownedKeys >= multiSigM) {
            useAutoSign = true;
        }
    } else if (isHashLock && !hashLockPubkey.empty()) {
        CPubKey pubKey(ParseHex(hashLockPubkey));
        if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
            useAutoSign = true;
        }
    }

    if (useAutoSign) {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        uint256 hash = SignatureHash(utxo.scriptPubKey, rawTx, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);

        if (isTimeLock) {
            CPubKey pubKey(ParseHex(timeLockOwnerPubkey));
            CKey key;
            if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                std::vector<unsigned char> vchSig;
                if (key.Sign(hash, vchSig)) {
                    vchSig.push_back((unsigned char)SIGHASH_ALL);
                    scriptSig << OP_1 << vchSig;
                }
            }
        } else if (isMultiSig) {
            std::vector<std::vector<unsigned char>> sigs;
            int foundSigs = 0;
            for (const auto& pkHex : multiSigPubkeys) {
                CPubKey pubKey(ParseHex(pkHex));
                CKey key;
                if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                    std::vector<unsigned char> vchSig;
                    if (key.Sign(hash, vchSig)) {
                        vchSig.push_back((unsigned char)SIGHASH_ALL);
                        sigs.push_back(vchSig);
                        foundSigs++;
                    }
                }
                if (foundSigs >= multiSigM) break;
            }
            scriptSig << OP_0;
            for (const auto& sig : sigs) {
                scriptSig << sig;
            }
        } else if (isHashLock) {
            // Get preimage from Param1
            QString preimageStr = ui->lineEditParam1->text().trimmed();
            std::vector<unsigned char> preimageBytes;
            if (IsHex(preimageStr.toStdString())) {
                preimageBytes = ParseHex(preimageStr.toStdString());
            } else {
                std::string s = preimageStr.toStdString();
                preimageBytes.assign(s.begin(), s.end());
            }

            CPubKey pubKey(ParseHex(hashLockPubkey));
            CKey key;
            if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                std::vector<unsigned char> vchSig;
                if (key.Sign(hash, vchSig)) {
                    vchSig.push_back((unsigned char)SIGHASH_ALL);
                    // Stack: OP_1 <sig> <preimage>
                    scriptSig << OP_1 << vchSig << preimageBytes;
                }
            }
        }
    } else {
        // ── Manual Parameter Fallback ─────────────────────────────────────────
        // Extra sigs (multisig: needs OP_0 first)
        QString extraSigsText = ui->plainTextExtraSigs->toPlainText().trimmed();
        QStringList extraSigs;
        if (!extraSigsText.isEmpty())
            extraSigs = extraSigsText.split('\n', QString::SkipEmptyParts);

        if (!extraSigs.isEmpty()) {
            scriptSig << OP_0;
            for (const QString& sig : extraSigs) {
                std::string sigHex = sig.trimmed().toStdString();
                if (!IsHex(sigHex)) {
                    QMessageBox::critical(this, tr("Invalid Input"),
                        tr("Extra signature is not valid hex: %1").arg(sig));
                    return;
                }
                scriptSig << ParseHex(sigHex);
            }
        }

        // Param2: pubkey (optional)
        QString param2Str = ui->lineEditParam2->text().trimmed();
        if (!param2Str.isEmpty()) {
            if (!IsHex(param2Str.toStdString())) {
                QMessageBox::critical(this, tr("Invalid Input"), tr("Public key is not valid hex."));
                return;
            }
            scriptSig << ParseHex(param2Str.toStdString());
        }

        // Param1: signature or preimage (optional)
        QString param1Str = ui->lineEditParam1->text().trimmed();
        if (!param1Str.isEmpty()) {
            if (!IsHex(param1Str.toStdString())) {
                QMessageBox::critical(this, tr("Invalid Input"), tr("Signature / preimage is not valid hex."));
                return;
            }
            scriptSig << ParseHex(param1Str.toStdString());
        }
    }

    rawTx.vin[0].scriptSig = scriptSig;

    // ── Sign via wallet key store ─────────────────────────────────────────────
    // For contracts where the wallet holds the key, use SignSignature
    CTransaction prevTxFull;
    bool foundPrev = false;
    {
        LOCK(cs_main);
        LOCK(pwalletMain->cs_wallet);
        auto it = pwalletMain->mapWallet.find(utxo.txid);
        if (it != pwalletMain->mapWallet.end()) {
            prevTxFull = *static_cast<const CTransaction*>(&it->second);
            foundPrev = true;
        }
    }

    if (!foundPrev) {
        QMessageBox::critical(this, tr("Error"), tr("Could not find the contract transaction in wallet."));
        return;
    }

    // ── Commit ────────────────────────────────────────────────────────────────
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        // Wrap CMutableTransaction → CTransaction → CWalletTx
        CTransaction finalTx(rawTx);
        CWalletTx wtxSpend(pwalletMain, finalTx);
        wtxSpend.fTimeReceivedIsTxTime = 1;

        CReserveKey reservekey(pwalletMain);
        CWallet::CommitResult res = pwalletMain->CommitTransaction(wtxSpend, reservekey, g_connman.get());

        if (res.status != CWallet::CommitStatus::OK) {
            QString errMsg = QString::fromStdString(res.ToString());
            QMessageBox::critical(this, tr("Commit Failed"),
                tr("Failed to broadcast spending transaction:\n%1\n\nNote: For time-locked contracts, the lock time must have passed.").arg(errMsg));
            return;
        }

        walletModel->emitBalanceChanged();

        std::string txid = res.hashTx.GetHex();
        ui->labelRunStatus->setText(tr("Status: Contract executed! Spending TxID: %1")
                                        .arg(QString::fromStdString(txid)));
        inform(tr("Contract executed successfully!\nSpending TxID: %1").arg(QString::fromStdString(txid)));
    }

    // Refresh the UTXO list since the contract is now spent
    onRefreshContractsClicked();
}

// ═══════════════════════════════════════════════════════════════════
//  CUSTOM BUILDER & DEVELOPER MODE
// ═══════════════════════════════════════════════════════════════════

#include "qt/pivx/visualdesignerdialog.h"
#include <QInputDialog>

void SmartContractWidget::onAddActionClicked()
{
    QStringList items;
    items << tr("Time-Locked (lock-time)")
          << tr("Signature Check (check-signature-verification)")
          << tr("Multi-Signature (multi-signature)")
          << tr("Hash-Locked (hash160)");

    bool ok;
    QString item = QInputDialog::getItem(this, tr("Add Step"), tr("Select step type:"), items, 0, false, &ok);
    if (!ok || item.isEmpty()) return;

    UniValue node(UniValue::VOBJ);
    UniValue inputs(UniValue::VARR);

    if (item.contains("lock-time")) {
        int64_t lockTime = QInputDialog::getInt(this, tr("Time-Locked"), tr("Lock Until (Block or Timestamp):"), 150, 0, 2000000000, 1, &ok);
        if (!ok) return;

        node.pushKV("role", "lock-time");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Lock-Until");
        inp.pushKV("type", "timestamp-or-block-height");
        inp.pushKV("value", lockTime);
        inputs.push_back(inp);
    }
    else if (item.contains("check-signature-verification")) {
        QString pubkey = QInputDialog::getText(this, tr("Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, "", &ok);
        if (!ok || pubkey.isEmpty()) return;

        node.pushKV("role", "check-signature-verification");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Pubkey");
        inp.pushKV("type", "pubkey");
        inp.pushKV("value", pubkey.trimmed().toStdString());
        inputs.push_back(inp);
    }
    else if (item.contains("multi-signature")) {
        bool ok1, ok2, ok3;
        int m = QInputDialog::getInt(this, tr("Multi-Signature"), tr("Required Signatures (m):"), 2, 1, 20, 1, &ok1);
        if (!ok1) return;
        int n = QInputDialog::getInt(this, tr("Multi-Signature"), tr("Total Keys (n):"), 3, 1, 20, 1, &ok2);
        if (!ok2) return;
        QString keysStr = QInputDialog::getText(this, tr("Multi-Signature"), tr("Public Keys (space-separated):"), QLineEdit::Normal, "", &ok3);
        if (!ok3 || keysStr.isEmpty()) return;

        node.pushKV("role", "multi-signature");
        UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", m); inputs.push_back(inpM);
        UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", n); inputs.push_back(inpN);

        UniValue keysArray(UniValue::VARR);
        QStringList keysList = keysStr.split(' ', QString::SkipEmptyParts);
        for (const QString& key : keysList) {
            keysArray.push_back(key.trimmed().toStdString());
        }
        UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); inputs.push_back(inpSigs);
    }
    else if (item.contains("hash160")) {
        bool ok1, ok2;
        QString hashHex = QInputDialog::getText(this, tr("Hash-Locked"), tr("Hash160 of Preimage (Hex):"), QLineEdit::Normal, "", &ok1);
        if (!ok1 || hashHex.isEmpty()) return;
        QString pubkey = QInputDialog::getText(this, tr("Hash-Locked"), tr("Recipient Pubkey (Hex):"), QLineEdit::Normal, "", &ok2);
        if (!ok2 || pubkey.isEmpty()) return;

        node.pushKV("role", "hash160");
        UniValue inpHash(UniValue::VOBJ);
        inpHash.pushKV("name", "Hash160");
        inpHash.pushKV("type", "string-or-number");
        inpHash.pushKV("value", hashHex.trimmed().toStdString());
        inputs.push_back(inpHash);
        node.pushKV("inputs", inputs);
        customActions.push_back(node);

        // Also add signature verification for hash-locked recipient sig
        UniValue sigNode(UniValue::VOBJ);
        sigNode.pushKV("role", "check-signature-verification");
        UniValue inpSig(UniValue::VOBJ);
        inpSig.pushKV("name", "Pubkey");
        inpSig.pushKV("type", "pubkey");
        inpSig.pushKV("value", pubkey.trimmed().toStdString());
        UniValue sigInputs(UniValue::VARR);
        sigInputs.push_back(inpSig);
        sigNode.pushKV("inputs", sigInputs);
        customActions.push_back(sigNode);

        updateCustomTree();
        generateContract();
        return;
    }

    node.pushKV("inputs", inputs);
    customActions.push_back(node);

    updateCustomTree();
    generateContract();
}

void SmartContractWidget::onRemoveActionClicked()
{
    QTreeWidgetItem* current = ui->treeCustomActions->currentItem();
    if (!current) {
        QMessageBox::warning(this, tr("Selection Required"), tr("Please select a step from the tree to remove."));
        return;
    }
    int index = ui->treeCustomActions->indexOfTopLevelItem(current);
    if (index >= 0 && index < (int)customActions.size()) {
        UniValue newActions(UniValue::VARR);
        for (unsigned int i = 0; i < customActions.size(); ++i) {
            if ((int)i != index) {
                newActions.push_back(customActions[i]);
            }
        }
        customActions = newActions;
        updateCustomTree();
        generateContract();
    }
}

void SmartContractWidget::onMoveActionUp()
{
    QTreeWidgetItem* current = ui->treeCustomActions->currentItem();
    if (!current) return;
    int index = ui->treeCustomActions->indexOfTopLevelItem(current);
    if (index > 0 && index < (int)customActions.size()) {
        UniValue newActions(UniValue::VARR);
        for (int i = 0; i < (int)customActions.size(); ++i) {
            if (i == index - 1) {
                newActions.push_back(customActions[index]);
            } else if (i == index) {
                newActions.push_back(customActions[index - 1]);
            } else {
                newActions.push_back(customActions[i]);
            }
        }
        customActions = newActions;
        updateCustomTree();
        
        QTreeWidgetItem* newCurrent = ui->treeCustomActions->topLevelItem(index - 1);
        ui->treeCustomActions->setCurrentItem(newCurrent);
        
        generateContract();
    }
}

void SmartContractWidget::onMoveActionDown()
{
    QTreeWidgetItem* current = ui->treeCustomActions->currentItem();
    if (!current) return;
    int index = ui->treeCustomActions->indexOfTopLevelItem(current);
    if (index >= 0 && index < (int)customActions.size() - 1) {
        UniValue newActions(UniValue::VARR);
        for (int i = 0; i < (int)customActions.size(); ++i) {
            if (i == index) {
                newActions.push_back(customActions[index + 1]);
            } else if (i == index + 1) {
                newActions.push_back(customActions[index]);
            } else {
                newActions.push_back(customActions[i]);
            }
        }
        customActions = newActions;
        updateCustomTree();
        
        QTreeWidgetItem* newCurrent = ui->treeCustomActions->topLevelItem(index + 1);
        ui->treeCustomActions->setCurrentItem(newCurrent);
        
        generateContract();
    }
}

void SmartContractWidget::onOpenVisualDesignerClicked()
{
    VisualDesignerDialog dialog(customActions, this);
    if (dialog.exec() == QDialog::Accepted) {
        customActions = dialog.getActions();
        updateCustomTree();
        generateContract();
    }
}

void SmartContractWidget::onDeveloperModeToggled(bool checked)
{
    ui->textEditYAML->setReadOnly(!checked);
    if (checked) {
        ui->textEditYAML->setStyleSheet("background-color: #1e1e2f; color: #f8f8f2; font-family: monospace;");
    } else {
        ui->textEditYAML->setStyleSheet("");
        generateContract();
    }
}

// Simple YAML Node and Parser implementation
struct YamlNode {
    int lineNum;
    int indent;
    std::string key;
    std::string value;
    bool isListItem;
    std::vector<YamlNode> children;
};

static bool ParseYamlLines(const std::string& yamlStr, std::vector<YamlNode>& rootNodes, std::string& error) {
    std::vector<std::string> lines;
    {
        std::string currentLine;
        for (char c : yamlStr) {
            if (c == '\n' || c == '\r') {
                if (!currentLine.empty() || c == '\n') {
                    lines.push_back(currentLine);
                    currentLine.clear();
                }
            } else {
                currentLine.push_back(c);
            }
        }
        if (!currentLine.empty()) {
            lines.push_back(currentLine);
        }
    }

    struct StackItem {
        int indent;
        YamlNode* node;
    };
    std::vector<StackItem> stack;

    for (size_t lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        std::string line = lines[lineIdx];
        
        int indent = 0;
        while (indent < (int)line.size() && line[indent] == ' ') {
            indent++;
        }
        
        std::string content = line.substr(indent);
        size_t hashPos = content.find('#');
        if (hashPos != std::string::npos) {
            content = content.substr(0, hashPos);
        }
        
        while (!content.empty() && (content.back() == ' ' || content.back() == '\t')) {
            content.pop_back();
        }
        
        if (content.empty()) {
            continue;
        }

        YamlNode node;
        node.lineNum = lineIdx + 1;
        node.indent = indent;
        node.isListItem = false;

        if (content[0] == '-') {
            node.isListItem = true;
            content = content.substr(1);
            size_t start = 0;
            while (start < content.size() && (content[start] == ' ' || content[start] == '\t')) {
                start++;
            }
            content = content.substr(start);
        }

        size_t colonPos = content.find(':');
        if (colonPos != std::string::npos) {
            node.key = content.substr(0, colonPos);
            while (!node.key.empty() && (node.key.back() == ' ' || node.key.back() == '\t')) {
                node.key.pop_back();
            }
            std::string valPart = content.substr(colonPos + 1);
            size_t valStart = 0;
            while (valStart < valPart.size() && (valPart[valStart] == ' ' || valPart[valStart] == '\t')) {
                valStart++;
            }
            node.value = valPart.substr(valStart);
        } else {
            node.value = content;
        }

        if (node.value.size() >= 2 && node.value.front() == '"' && node.value.back() == '"') {
            node.value = node.value.substr(1, node.value.size() - 2);
        }

        while (!stack.empty() && stack.back().indent >= indent) {
            stack.pop_back();
        }

        if (stack.empty()) {
            rootNodes.push_back(node);
            stack.push_back({indent, &rootNodes.back()});
        } else {
            stack.back().node->children.push_back(node);
            stack.push_back({indent, &stack.back().node->children.back()});
        }
    }
    return true;
}

static UniValue ParsePrimitive(const std::string& val) {
    if (val == "true") return UniValue(true);
    if (val == "false") return UniValue(false);
    
    if (!val.empty()) {
        bool isNum = true;
        bool hasDot = false;
        size_t start = 0;
        if (val[0] == '-') start = 1;
        for (size_t i = start; i < val.size(); ++i) {
            if (val[i] == '.') {
                if (hasDot) { isNum = false; break; }
                hasDot = true;
            } else if (!std::isdigit(val[i])) {
                isNum = false;
                break;
            }
        }
        if (isNum) {
            try {
                if (hasDot) {
                    return UniValue(std::stod(val));
                } else {
                    return UniValue((int64_t)std::stoll(val));
                }
            } catch (...) {}
        }
    }
    return UniValue(val);
}

static UniValue ConvertNodeChildrenToUniValue(const std::vector<YamlNode>& children, std::string& error) {
    if (children.empty()) {
        return UniValue("");
    }

    bool isArray = false;
    for (const auto& child : children) {
        if (child.isListItem) {
            isArray = true;
            break;
        }
    }

    if (isArray) {
        UniValue arr(UniValue::VARR);
        for (const auto& child : children) {
            if (!child.children.empty()) {
                arr.push_back(ConvertNodeChildrenToUniValue(child.children, error));
            } else {
                arr.push_back(ParsePrimitive(child.value));
            }
        }
        return arr;
    } else {
        UniValue obj(UniValue::VOBJ);
        for (const auto& child : children) {
            if (child.key.empty()) {
                if (children.size() == 1) {
                    return ParsePrimitive(child.value);
                }
                continue;
            }
            if (!child.children.empty()) {
                obj.pushKV(child.key, ConvertNodeChildrenToUniValue(child.children, error));
            } else {
                obj.pushKV(child.key, ParsePrimitive(child.value));
            }
        }
        return obj;
    }
}

void SmartContractWidget::onYAMLTextChanged()
{
    if (!ui->cbDeveloperMode->isChecked()) {
        return;
    }

    std::string yamlStr = ui->textEditYAML->toPlainText().toStdString();
    std::vector<YamlNode> rootNodes;
    std::string error;
    
    if (!ParseYamlLines(yamlStr, rootNodes, error)) {
        ui->textEditJSON->setText(tr("YAML Parse Error: %1").arg(QString::fromStdString(error)));
        return;
    }

    UniValue doc = ConvertNodeChildrenToUniValue(rootNodes, error);
    if (!error.empty()) {
        ui->textEditJSON->setText(tr("YAML Conversion Error: %1").arg(QString::fromStdString(error)));
        return;
    }

    activeDoc = doc;
    
    if (activeDoc.exists("active_contract")) {
        activeName = activeDoc["active_contract"].get_str();
    } else {
        activeName = "CustomContract";
    }

    ui->textEditJSON->setText(QString::fromStdString(activeDoc.write(2)));

    std::string compileErr;
    CMescal::Compile(activeDoc.write(), compileErr);
    if (!compileErr.empty()) {
        ui->labelStatus->setStyleSheet("color: #ff3d00;");
        ui->labelStatus->setText(tr("Status: Validation Failed! %1").arg(QString::fromStdString(compileErr)));
    } else {
        ui->labelStatus->setStyleSheet("color: #26a69a;");
        ui->labelStatus->setText(tr("Status: Contract Valid!"));
    }
}

void SmartContractWidget::updateCustomTree()
{
    ui->treeCustomActions->clear();
    for (unsigned int i = 0; i < customActions.size(); ++i) {
        const UniValue& act = customActions[i];
        std::string role = act["role"].get_str();
        
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->treeCustomActions);
        item->setText(0, QString("Step %1: %2").arg(i + 1).arg(QString::fromStdString(role)));
        
        QString details = "";
        const UniValue& inputs = act["inputs"];
        if (inputs.isArray()) {
            for (unsigned int j = 0; j < inputs.size(); ++j) {
                details += QString("%1: %2 | ")
                    .arg(QString::fromStdString(inputs[j]["name"].get_str()))
                    .arg(QString::fromStdString(inputs[j]["value"].getValStr()));
            }
            if (details.endsWith(" | ")) {
                details.chop(3);
            }
        }
        item->setText(1, details);
    }
}

void SmartContractWidget::buildContractFromCustom()
{
    activeName = "CustomContract";
    UniValue doc(UniValue::VOBJ);
    UniValue basic(UniValue::VOBJ);
    UniValue contract(UniValue::VOBJ);
    UniValue actions(UniValue::VARR);

    for (unsigned int i = 0; i < customActions.size(); ++i) {
        std::string stepName = "Step-" + std::to_string(i + 1);
        UniValue stepSpec(UniValue::VOBJ);
        stepSpec.pushKV("role", customActions[i]["role"].get_str());
        stepSpec.pushKV("inputs", customActions[i]["inputs"]);
        basic.pushKV(stepName, stepSpec);

        UniValue actRef(UniValue::VOBJ);
        actRef.pushKV("type", "basic");
        actRef.pushKV("name", stepName);
        actions.push_back(actRef);
    }

    UniValue cSpec(UniValue::VOBJ);
    cSpec.pushKV("description", "Custom smart contract built using Visual/Custom Builder.");
    cSpec.pushKV("actions", actions);
    contract.pushKV(activeName, cSpec);

    doc.pushKV("basic", basic);
    doc.pushKV("contract", contract);
    doc.pushKV("active_contract", activeName);

    activeDoc = doc;
    updatePreviews();
}

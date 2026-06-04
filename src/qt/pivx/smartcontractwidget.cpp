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
#include "net.h"
#include "main.h" // for cs_main and COIN
#include "coincontrol.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QFile>

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
        result += (val.get_bool() ? "true" : "false") + "\n";
    } else {
        result += "\"" + val.get_str() + "\"\n";
    }
    return result;
}

SmartContractWidget::SmartContractWidget(PIVXGUI* parent) :
    PWidget(parent),
    ui(new Ui::SmartContractWidget)
{
    ui->setupUi(this);

    this->setStyleSheet(parent->styleSheet());

    // Style containers
    setCssProperty(ui->left, "container");
    ui->left->setContentsMargins(20, 20, 20, 20);
    setCssProperty(ui->right, "container-right");
    ui->right->setContentsMargins(20, 20, 20, 20);

    // Style titles
    setCssProperty(ui->labelTitle, "text-title-screen");
    setCssProperty(ui->labelSubtitle1, "text-subtitle");
    setCssProperty(ui->labelTitleRight, "text-title-screen");
    setCssProperty(ui->lblYAML, "text-title");
    setCssProperty(ui->lblJSON, "text-title");

    setCssProperty({ui->lblTLName, ui->lblTLExpiry, ui->lblTLPubkey,
                    ui->lblMSName, ui->lblMSN, ui->lblMSM, ui->lblMSKeys,
                    ui->lblHLName, ui->lblHLHash, ui->lblHLPubkey, ui->labelAmount}, "text-title");

    // Style input edits
    setCssEditLine({ui->lineEditNameTimeLock, ui->lineEditExpiryTimeLock, ui->lineEditPubkeyTimeLock,
                    ui->lineEditNameMultiSig, ui->lineEditNMultiSig, ui->lineEditMMultiSig,
                    ui->lineEditNameHashLock, ui->lineEditHashHashLock, ui->lineEditPubkeyHashLock,
                    ui->lineEditAmount}, true);

    // Style buttons
    setCssBtnSecondary(ui->btnSave);
    setCssBtnPrimary(ui->btnPublish);

    // Connect text changes for live compilation/preview
    connect(ui->lineEditNameTimeLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditExpiryTimeLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditPubkeyTimeLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);

    connect(ui->lineEditNameMultiSig, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditNMultiSig, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditMMultiSig, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->plainTextEditKeysMultiSig, &QPlainTextEdit::textChanged, this, &SmartContractWidget::generateContract);

    connect(ui->lineEditNameHashLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditHashHashLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditPubkeyHashLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);

    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &SmartContractWidget::generateContract);

    // Connect bottom buttons
    connect(ui->btnSave, &QPushButton::clicked, this, &SmartContractWidget::onSaveClicked);
    connect(ui->btnPublish, &QPushButton::clicked, this, &SmartContractWidget::onPublishClicked);

    // Initial generation
    generateContract();
}

SmartContractWidget::~SmartContractWidget()
{
    delete ui;
}

void SmartContractWidget::loadWalletModel()
{
    if (walletModel) {
        // Pre-populate fields with a pubkey from user wallet keypool if available
        CPubKey pubKey;
        if (pwalletMain && pwalletMain->GetKeyFromPool(pubKey)) {
            std::string pubkeyHex = HexStr(pubKey.begin(), pubKey.end());
            ui->lineEditPubkeyTimeLock->setText(QString::fromStdString(pubkeyHex));
            ui->lineEditPubkeyHashLock->setText(QString::fromStdString(pubkeyHex));
        }
        generateContract();
    }
}

void SmartContractWidget::generateContract()
{
    int tabIndex = ui->tabWidget->currentIndex();
    UniValue doc(UniValue::VOBJ);
    UniValue basic(UniValue::VOBJ);
    UniValue contract(UniValue::VOBJ);

    if (tabIndex == 0) { // Time-Locked
        std::string name = ui->lineEditNameTimeLock->text().toStdString();
        std::string expiryStr = ui->lineEditExpiryTimeLock->text().toStdString();
        UniValue expiryVal;
        try {
            expiryVal = UniValue(std::stoll(expiryStr));
        } catch (...) {
            expiryVal = UniValue(expiryStr);
        }
        std::string pubkey = ui->lineEditPubkeyTimeLock->text().toStdString();

        activeName = name;

        UniValue tlInputs(UniValue::VARR);
        UniValue tlInput1(UniValue::VOBJ);
        tlInput1.pushKV("name", "Lock-Until");
        tlInput1.pushKV("type", "timestamp-or-block-height");
        tlInput1.pushKV("value", expiryVal);
        tlInputs.push_back(tlInput1);

        UniValue tlSpec(UniValue::VOBJ);
        tlSpec.pushKV("role", "lock-time");
        tlSpec.pushKV("inputs", tlInputs);
        basic.pushKV("Lock-Time", tlSpec);

        UniValue osInputs(UniValue::VARR);
        UniValue osInput1(UniValue::VOBJ);
        osInput1.pushKV("name", "Pubkey");
        osInput1.pushKV("type", "pubkey");
        osInput1.pushKV("value", pubkey);
        osInputs.push_back(osInput1);

        UniValue osSpec(UniValue::VOBJ);
        osSpec.pushKV("role", "check-signature-verification");
        osSpec.pushKV("inputs", osInputs);
        basic.pushKV("Owner-Sig", osSpec);

        UniValue cSpec(UniValue::VOBJ);
        cSpec.pushKV("description", "Funds locked requiring owner signature.");
        UniValue actions(UniValue::VARR);
        UniValue action1(UniValue::VOBJ);
        action1.pushKV("type", "basic");
        action1.pushKV("name", "Lock-Time");
        actions.push_back(action1);
        UniValue action2(UniValue::VOBJ);
        action2.pushKV("type", "basic");
        action2.pushKV("name", "Owner-Sig");
        actions.push_back(action2);
        cSpec.pushKV("actions", actions);
        contract.pushKV(name, cSpec);

        doc.pushKV("basic", basic);
        doc.pushKV("contract", contract);
        doc.pushKV("active_contract", name);

    } else if (tabIndex == 1) { // Multi-Signature
        std::string name = ui->lineEditNameMultiSig->text().toStdString();
        int n = ui->lineEditNMultiSig->text().toInt();
        int m = ui->lineEditMMultiSig->text().toInt();
        QString keysText = ui->plainTextEditKeysMultiSig->toPlainText();
        QStringList keysList = keysText.split('\n', QString::SkipEmptyParts);

        activeName = name;

        UniValue msInputs(UniValue::VARR);
        UniValue msInput1(UniValue::VOBJ);
        msInput1.pushKV("name", "m");
        msInput1.pushKV("type", "number");
        msInput1.pushKV("value", m);
        msInputs.push_back(msInput1);

        UniValue msInput2(UniValue::VOBJ);
        msInput2.pushKV("name", "n");
        msInput2.pushKV("type", "number");
        msInput2.pushKV("value", n);
        msInputs.push_back(msInput2);

        UniValue keysArray(UniValue::VARR);
        for (const QString& k : keysList) {
            keysArray.push_back(k.trimmed().toStdString());
        }
        UniValue msInput3(UniValue::VOBJ);
        msInput3.pushKV("name", "Signatures");
        msInput3.pushKV("type", "array");
        msInput3.pushKV("value", keysArray);
        msInputs.push_back(msInput3);

        UniValue msSpec(UniValue::VOBJ);
        msSpec.pushKV("role", "multi-signature");
        msSpec.pushKV("inputs", msInputs);
        basic.pushKV("Multi-Signature", msSpec);

        UniValue cSpec(UniValue::VOBJ);
        cSpec.pushKV("description", std::to_string(m) + "-of-" + std::to_string(n) + " multi-signature smart contract.");
        UniValue actions(UniValue::VARR);
        UniValue action1(UniValue::VOBJ);
        action1.pushKV("type", "basic");
        action1.pushKV("name", "Multi-Signature");
        actions.push_back(action1);
        cSpec.pushKV("actions", actions);
        contract.pushKV(name, cSpec);

        doc.pushKV("basic", basic);
        doc.pushKV("contract", contract);
        doc.pushKV("active_contract", name);

    } else if (tabIndex == 2) { // Hash-Locked
        std::string name = ui->lineEditNameHashLock->text().toStdString();
        std::string hash160 = ui->lineEditHashHashLock->text().toStdString();
        std::string pubkey = ui->lineEditPubkeyHashLock->text().toStdString();

        activeName = name;

        UniValue hcInputs(UniValue::VARR);
        UniValue hcInput1(UniValue::VOBJ);
        hcInput1.pushKV("name", "Hash160");
        hcInput1.pushKV("type", "string-or-number");
        hcInput1.pushKV("value", hash160);
        hcInputs.push_back(hcInput1);

        UniValue hcSpec(UniValue::VOBJ);
        hcSpec.pushKV("role", "hash160");
        hcSpec.pushKV("inputs", hcInputs);
        basic.pushKV("Hash-Check", hcSpec);

        UniValue rsInputs(UniValue::VARR);
        UniValue rsInput1(UniValue::VOBJ);
        rsInput1.pushKV("name", "Pubkey");
        rsInput1.pushKV("type", "pubkey");
        rsInput1.pushKV("value", pubkey);
        rsInputs.push_back(rsInput1);

        UniValue rsSpec(UniValue::VOBJ);
        rsSpec.pushKV("role", "check-signature-verification");
        rsSpec.pushKV("inputs", rsInputs);
        basic.pushKV("Recipient-Sig", rsSpec);

        UniValue cSpec(UniValue::VOBJ);
        cSpec.pushKV("description", "Requires revealing preimage and recipient signature.");
        UniValue actions(UniValue::VARR);
        UniValue action1(UniValue::VOBJ);
        action1.pushKV("type", "basic");
        action1.pushKV("name", "Hash-Check");
        actions.push_back(action1);
        UniValue action2(UniValue::VOBJ);
        action2.pushKV("type", "basic");
        action2.pushKV("name", "Recipient-Sig");
        actions.push_back(action2);
        cSpec.pushKV("actions", actions);
        contract.pushKV(name, cSpec);

        doc.pushKV("basic", basic);
        doc.pushKV("contract", contract);
        doc.pushKV("active_contract", name);
    }

    activeDoc = doc;
    updatePreviews();
}

void SmartContractWidget::updatePreviews()
{
    std::string jsonStr = activeDoc.write(2);
    ui->textEditJSON->setText(QString::fromStdString(jsonStr));

    std::string yamlStr = UniValueToYAML(activeDoc, 0);
    ui->textEditYAML->setText(QString::fromStdString(yamlStr));
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

    // Save JSON
    QFile jsonFile(path);
    if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&jsonFile);
        out << QString::fromStdString(activeDoc.write(2));
        jsonFile.close();
    }

    // Save YAML next to it
    QString yamlPath = path;
    if (yamlPath.endsWith(".json")) {
        yamlPath.chop(5);
        yamlPath.append(".yaml");
    } else {
        yamlPath.append(".yaml");
    }
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
        QMessageBox::critical(this, tr("Compilation Error"), tr("MESCAL compilation failed:\n%1").arg(QString::fromStdString(errorStr)));
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

    // 4. Send transaction directly via pwalletMain
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
        inform(tr("Transaction commit failed."));
        return;
    }

    std::string txid = wtx.GetHash().GetHex();
    ui->labelStatus->setText(tr("Status: Contract published successfully! TxID: %1").arg(QString::fromStdString(txid)));
    inform(tr("Contract published successfully!\nTxID: %1").arg(QString::fromStdString(txid)));
}

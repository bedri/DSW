// Copyright (c) 2026 The DSW developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/pivx/visualdesignerdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>
#include <QDebug>

DesignerNodeItem::DesignerNodeItem(const QString& name, const QString& details, const UniValue& data, QGraphicsItem* parent)
    : QGraphicsRectItem(parent), name(name), details(details), nodeData(data)
{
    setRect(0, 0, 200, 70);
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
}

void DesignerNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    // Draw rounded background block
    painter->setRenderHint(QPainter::Antialiasing);
    
    QPen pen(QColor("#26a69a"));
    if (isSelected()) {
        pen.setColor(QColor("#ff9100"));
        pen.setWidth(3);
    } else {
        pen.setWidth(2);
    }
    
    painter->setPen(pen);
    painter->setBrush(QBrush(QColor("#1b1b2a")));
    painter->drawRoundedRect(rect(), 8, 8);
    
    // Draw Name
    painter->setPen(QPen(QColor("#ffffff")));
    QFont fontName = painter->font();
    fontName.setBold(true);
    painter->setFont(fontName);
    painter->drawText(QRectF(10, 10, 180, 20), Qt::AlignLeft, name);
    
    // Draw Details
    painter->setPen(QPen(QColor("#b0bec5")));
    QFont fontDetails = painter->font();
    fontDetails.setBold(false);
    fontDetails.setPointSize(8);
    painter->setFont(fontDetails);
    painter->drawText(QRectF(10, 35, 180, 30), Qt::AlignLeft | Qt::TextWordWrap, details);
}

VisualDesignerDialog::VisualDesignerDialog(const UniValue& initialActions, QWidget* parent)
    : QDialog(parent), currentActions(initialActions)
{
    setWindowTitle(tr("Visual Smart Contract Designer"));
    resize(1000, 700);
    
    setupLayout();
    rebuildScene();
}

VisualDesignerDialog::~VisualDesignerDialog()
{
    delete scene;
}

void VisualDesignerDialog::setupLayout()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    
    // Left: visual graphics scene
    scene = new QGraphicsScene(this);
    scene->setSceneRect(0, 0, 600, 800);
    scene->setBackgroundBrush(QBrush(QColor("#0d0d1b")));
    
    view = new QGraphicsView(scene, this);
    view->setRenderHint(QPainter::Antialiasing);
    view->setDragMode(QGraphicsView::RubberBandDrag);
    view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    
    mainLayout->addWidget(view, 3);
    
    // Right: control panel
    QVBoxLayout* rightLayout = new QVBoxLayout();
    
    listNodes = new QListWidget(this);
    listNodes->addItem(tr("Time-Locked (lock-time)"));
    listNodes->addItem(tr("Signature Check (check-signature-verification)"));
    listNodes->addItem(tr("Multi-Signature (multi-signature)"));
    listNodes->addItem(tr("Hash-Locked (hash160)"));
    listNodes->addItem(tr("IF-Condition (if-condition)"));
    
    rightLayout->addWidget(listNodes);
    
    btnAdd = new QPushButton(tr("+ Add Selected to Canvas"), this);
    btnRemove = new QPushButton(tr("- Delete Selected Block"), this);
    btnApply = new QPushButton(tr("Apply Changes"), this);
    btnCancel = new QPushButton(tr("Cancel"), this);
    
    rightLayout->addWidget(btnAdd);
    rightLayout->addWidget(btnRemove);
    rightLayout->addStretch();
    rightLayout->addWidget(btnApply);
    rightLayout->addWidget(btnCancel);
    
    mainLayout->addLayout(rightLayout, 1);
    
    connect(btnAdd, &QPushButton::clicked, this, &VisualDesignerDialog::onAddNodeClicked);
    connect(btnRemove, &QPushButton::clicked, this, &VisualDesignerDialog::onRemoveNodeClicked);
    connect(btnApply, &QPushButton::clicked, this, &VisualDesignerDialog::onApplyClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

void VisualDesignerDialog::rebuildScene()
{
    qDebug() << "VisualDesignerDialog::rebuildScene() - currentActions size:" << currentActions.size();
    scene->clear();
    nodeItems.clear();
    
    int yOffset = 40;
    
    for (unsigned int i = 0; i < currentActions.size(); i++) {
        const UniValue& act = currentActions[i];
        std::string role = act["role"].get_str();
        
        QString qName = QString::fromStdString(role);
        QString qDetails = "";
        
        if (role == "if-condition") {
            qName = "IF-Condition";
            UniValue expr = act["expression"];
            qDetails = QString("IF [%1]\nTHEN [%2]")
                .arg(QString::fromStdString(expr["role"].get_str()))
                .arg(QString::fromStdString(act["true_action"]["role"].get_str()));
            if (act.exists("false_action") && act["false_action"].isObject()) {
                qDetails += QString("\nELSE [%1]").arg(QString::fromStdString(act["false_action"]["role"].get_str()));
            }
        } else {
            const UniValue& inputs = act["inputs"];
            if (inputs.isArray() && !inputs.empty()) {
                for (unsigned int j = 0; j < inputs.size(); j++) {
                    qDetails += QString("%1: %2\n")
                        .arg(QString::fromStdString(inputs[j]["name"].get_str()))
                        .arg(QString::fromStdString(inputs[j]["value"].getValStr()));
                }
            }
        }
        
        DesignerNodeItem* item = new DesignerNodeItem(qName, qDetails.trimmed(), act);
        item->setPos(200, yOffset);
        scene->addItem(item);
        nodeItems.append(item);
        
        // Draw connector line if not the first item
        if (i > 0) {
            QGraphicsLineItem* line = new QGraphicsLineItem(300, yOffset - 40, 300, yOffset);
            line->setPen(QPen(QColor("#26a69a"), 2, Qt::DashLine));
            scene->addItem(line);
        }
        
        yOffset += 110;
    }
}

void VisualDesignerDialog::onAddNodeClicked()
{
    int row = listNodes->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, tr("Selection Required"), tr("Please select a block type to add from the list."));
        return;
    }
    
    UniValue node(UniValue::VOBJ);
    UniValue inputs(UniValue::VARR);
    
    if (row == 0) { // Time-Locked
        bool ok;
        int64_t lockTime = QInputDialog::getInt(this, tr("Time-Locked"), tr("Lock Until (Block or Timestamp):"), 150, 0, 2000000000, 1, &ok);
        if (!ok) return;
        
        node.pushKV("role", "lock-time");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Lock-Until");
        inp.pushKV("type", "timestamp-or-block-height");
        inp.pushKV("value", lockTime);
        inputs.push_back(inp);
    }
    else if (row == 1) { // Signature Check
        bool ok;
        QString pubkey = QInputDialog::getText(this, tr("Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, "", &ok);
        if (!ok || pubkey.isEmpty()) return;
        
        node.pushKV("role", "check-signature-verification");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Pubkey");
        inp.pushKV("type", "pubkey");
        inp.pushKV("value", pubkey.trimmed().toStdString());
        inputs.push_back(inp);
    }
    else if (row == 2) { // Multi-Signature
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
    else if (row == 3) { // Hash-Locked
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
        
        currentActions.push_back(node);
        
        // Also automatically add check-signature-verification for Hash-Locked recipient sig
        UniValue sigNode(UniValue::VOBJ);
        sigNode.pushKV("role", "check-signature-verification");
        UniValue inpSig(UniValue::VOBJ);
        inpSig.pushKV("name", "Pubkey");
        inpSig.pushKV("type", "pubkey");
        inpSig.pushKV("value", pubkey.trimmed().toStdString());
        UniValue sigInputs(UniValue::VARR);
        sigInputs.push_back(inpSig);
        sigNode.pushKV("inputs", sigInputs);
        
        currentActions.push_back(sigNode);
        qDebug() << "onAddNodeClicked - Added Hash-Locked & Sig-Check nodes. currentActions size:" << currentActions.size();
        rebuildScene();
        return;
    }
    
    else if (row == 4) { // IF-Condition
        bool ok;
        QStringList exprTypes;
        exprTypes << tr("Signature Check (check-signature-verification)")
                  << tr("Hash-Locked (hash160)");
        QString exprType = QInputDialog::getItem(this, tr("IF-Condition - Expression"), tr("Select condition check type:"), exprTypes, 0, false, &ok);
        if (!ok || exprType.isEmpty()) return;

        UniValue exprAct(UniValue::VOBJ);
        UniValue exprInputs(UniValue::VARR);
        if (exprType.contains("check-signature-verification")) {
            QString pubkey = QInputDialog::getText(this, tr("Expression - Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, "", &ok);
            if (!ok || pubkey.isEmpty()) return;
            exprAct.pushKV("role", "check-signature-verification");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Pubkey"); inp.pushKV("type", "pubkey"); inp.pushKV("value", pubkey.trimmed().toStdString());
            exprInputs.push_back(inp);
        } else {
            bool ok1, ok2;
            QString hashHex = QInputDialog::getText(this, tr("Expression - Hash-Locked"), tr("Hash160 of Preimage (Hex):"), QLineEdit::Normal, "", &ok1);
            if (!ok1 || hashHex.isEmpty()) return;
            QString pubkey = QInputDialog::getText(this, tr("Expression - Hash-Locked"), tr("Recipient Pubkey (Hex):"), QLineEdit::Normal, "", &ok2);
            if (!ok2 || pubkey.isEmpty()) return;
            exprAct.pushKV("role", "hash160");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Hash160"); inp.pushKV("type", "string-or-number"); inp.pushKV("value", hashHex.trimmed().toStdString());
            exprInputs.push_back(inp);
        }
        exprAct.pushKV("inputs", exprInputs);

        QStringList actionTypes;
        actionTypes << tr("Time-Locked (lock-time)")
                    << tr("Signature Check (check-signature-verification)")
                    << tr("Multi-Signature (multi-signature)")
                    << tr("Hash-Locked (hash160)");
        QString trueType = QInputDialog::getItem(this, tr("IF-Condition - True Branch"), tr("Select action when TRUE:"), actionTypes, 0, false, &ok);
        if (!ok || trueType.isEmpty()) return;

        UniValue trueAct(UniValue::VOBJ);
        UniValue trueInputs(UniValue::VARR);
        if (trueType.contains("lock-time")) {
            int64_t lockTime = QInputDialog::getInt(this, tr("True Branch - Time-Locked"), tr("Lock Until (Block or Timestamp):"), 150, 0, 2000000000, 1, &ok);
            if (!ok) return;
            trueAct.pushKV("role", "lock-time");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Lock-Until"); inp.pushKV("type", "timestamp-or-block-height"); inp.pushKV("value", lockTime); trueInputs.push_back(inp);
        } else if (trueType.contains("check-signature-verification")) {
            QString pubkey = QInputDialog::getText(this, tr("True Branch - Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, "", &ok);
            if (!ok || pubkey.isEmpty()) return;
            trueAct.pushKV("role", "check-signature-verification");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Pubkey"); inp.pushKV("type", "pubkey"); inp.pushKV("value", pubkey.trimmed().toStdString()); trueInputs.push_back(inp);
        } else if (trueType.contains("multi-signature")) {
            bool ok1, ok2, ok3;
            int m = QInputDialog::getInt(this, tr("True Branch - Multi-Signature"), tr("Required Signatures (m):"), 2, 1, 20, 1, &ok1);
            if (!ok1) return;
            int n = QInputDialog::getInt(this, tr("True Branch - Multi-Signature"), tr("Total Keys (n):"), 3, 1, 20, 1, &ok2);
            if (!ok2) return;
            QString keysStr = QInputDialog::getText(this, tr("True Branch - Multi-Signature"), tr("Public Keys (space-separated):"), QLineEdit::Normal, "", &ok3);
            if (!ok3 || keysStr.isEmpty()) return;
            trueAct.pushKV("role", "multi-signature");
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", m); trueInputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", n); trueInputs.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            QStringList keysList = keysStr.split(' ', QString::SkipEmptyParts);
            for (const QString& key : keysList) keysArray.push_back(key.trimmed().toStdString());
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); trueInputs.push_back(inpSigs);
        } else if (trueType.contains("hash160")) {
            bool ok1, ok2;
            QString hashHex = QInputDialog::getText(this, tr("True Branch - Hash-Locked"), tr("Hash160 of Preimage (Hex):"), QLineEdit::Normal, "", &ok1);
            if (!ok1 || hashHex.isEmpty()) return;
            QString pubkey = QInputDialog::getText(this, tr("True Branch - Hash-Locked"), tr("Recipient Pubkey (Hex):"), QLineEdit::Normal, "", &ok2);
            if (!ok2 || pubkey.isEmpty()) return;
            trueAct.pushKV("role", "hash160");
            UniValue inpHash(UniValue::VOBJ); inpHash.pushKV("name", "Hash160"); inpHash.pushKV("type", "string-or-number"); inpHash.pushKV("value", hashHex.trimmed().toStdString()); trueInputs.push_back(inpHash);
        }
        trueAct.pushKV("inputs", trueInputs);

        QStringList falseActionTypes;
        falseActionTypes << tr("None")
                         << tr("Time-Locked (lock-time)")
                         << tr("Signature Check (check-signature-verification)")
                         << tr("Multi-Signature (multi-signature)")
                         << tr("Hash-Locked (hash160)");
        QString falseType = QInputDialog::getItem(this, tr("IF-Condition - False Branch"), tr("Select action when FALSE:"), falseActionTypes, 0, false, &ok);
        if (!ok || falseType.isEmpty()) return;

        UniValue falseAct(UniValue::VOBJ);
        UniValue falseInputs(UniValue::VARR);
        bool hasFalse = false;
        if (falseType.contains("lock-time")) {
            int64_t lockTime = QInputDialog::getInt(this, tr("False Branch - Time-Locked"), tr("Lock Until (Block or Timestamp):"), 150, 0, 2000000000, 1, &ok);
            if (!ok) return;
            falseAct.pushKV("role", "lock-time");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Lock-Until"); inp.pushKV("type", "timestamp-or-block-height"); inp.pushKV("value", lockTime); falseInputs.push_back(inp);
            hasFalse = true;
        } else if (falseType.contains("check-signature-verification")) {
            QString pubkey = QInputDialog::getText(this, tr("False Branch - Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, "", &ok);
            if (!ok || pubkey.isEmpty()) return;
            falseAct.pushKV("role", "check-signature-verification");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Pubkey"); inp.pushKV("type", "pubkey"); inp.pushKV("value", pubkey.trimmed().toStdString()); falseInputs.push_back(inp);
            hasFalse = true;
        } else if (falseType.contains("multi-signature")) {
            bool ok1, ok2, ok3;
            int m = QInputDialog::getInt(this, tr("False Branch - Multi-Signature"), tr("Required Signatures (m):"), 2, 1, 20, 1, &ok1);
            if (!ok1) return;
            int n = QInputDialog::getInt(this, tr("False Branch - Multi-Signature"), tr("Total Keys (n):"), 3, 1, 20, 1, &ok2);
            if (!ok2) return;
            QString keysStr = QInputDialog::getText(this, tr("False Branch - Multi-Signature"), tr("Public Keys (space-separated):"), QLineEdit::Normal, "", &ok3);
            if (!ok3 || keysStr.isEmpty()) return;
            falseAct.pushKV("role", "multi-signature");
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", m); falseInputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", n); falseInputs.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            QStringList keysList = keysStr.split(' ', QString::SkipEmptyParts);
            for (const QString& key : keysList) keysArray.push_back(key.trimmed().toStdString());
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); falseInputs.push_back(inpSigs);
            hasFalse = true;
        } else if (falseType.contains("hash160")) {
            bool ok1, ok2;
            QString hashHex = QInputDialog::getText(this, tr("False Branch - Hash-Locked"), tr("Hash160 of Preimage (Hex):"), QLineEdit::Normal, "", &ok1);
            if (!ok1 || hashHex.isEmpty()) return;
            QString pubkey = QInputDialog::getText(this, tr("False Branch - Hash-Locked"), tr("Recipient Pubkey (Hex):"), QLineEdit::Normal, "", &ok2);
            if (!ok2 || pubkey.isEmpty()) return;
            falseAct.pushKV("role", "hash160");
            UniValue inpHash(UniValue::VOBJ); inpHash.pushKV("name", "Hash160"); inpHash.pushKV("type", "string-or-number"); inpHash.pushKV("value", hashHex.trimmed().toStdString()); falseInputs.push_back(inpHash);
            hasFalse = true;
        }

        UniValue condNode(UniValue::VOBJ);
        condNode.pushKV("role", "if-condition");
        condNode.pushKV("expression", exprAct);
        condNode.pushKV("true_action", trueAct);
        if (hasFalse) {
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);
        }

        currentActions.push_back(condNode);
        qDebug() << "onAddNodeClicked - Added IF-Condition node. currentActions size:" << currentActions.size();
        rebuildScene();
        return;
    }
    
    node.pushKV("inputs", inputs);
    currentActions.push_back(node);
    qDebug() << "onAddNodeClicked - Added node. currentActions size:" << currentActions.size();
    
    rebuildScene();
}

void VisualDesignerDialog::onRemoveNodeClicked()
{
    QList<QGraphicsItem*> selected = scene->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::warning(this, tr("Selection Required"), tr("Please click on a block item on the canvas to select it."));
        return;
    }
    
    DesignerNodeItem* target = dynamic_cast<DesignerNodeItem*>(selected.first());
    if (!target) return;
    
    int index = nodeItems.indexOf(target);
    if (index >= 0) {
        // Remove from currentActions array
        UniValue newActions(UniValue::VARR);
        for (unsigned int i = 0; i < currentActions.size(); i++) {
            if ((int)i != index) {
                newActions.push_back(currentActions[i]);
            }
        }
        currentActions = newActions;
        rebuildScene();
    }
}

void VisualDesignerDialog::onApplyClicked()
{
    // Reorder actions array based on their vertical Y coordinate on the canvas!
    // This allows users to visually reorder blocks by dragging them up or down!
    QList<DesignerNodeItem*> sortedNodes = nodeItems;
    std::sort(sortedNodes.begin(), sortedNodes.end(), [](DesignerNodeItem* a, DesignerNodeItem* b) {
        return a->y() < b->y();
    });
    
    UniValue reorderedActions(UniValue::VARR);
    for (DesignerNodeItem* item : sortedNodes) {
        reorderedActions.push_back(item->nodeData);
    }
    currentActions = reorderedActions;
    
    accept();
}

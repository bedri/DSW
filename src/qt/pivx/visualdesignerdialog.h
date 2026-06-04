// Copyright (c) 2026 The DSW developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VISUALDESIGNERDIALOG_H
#define VISUALDESIGNERDIALOG_H

#include <QDialog>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QPushButton>
#include <QListWidget>
#include <univalue.h>

class DesignerNodeItem : public QGraphicsRectItem {
public:
    DesignerNodeItem(const QString& name, const QString& details, const UniValue& data, QGraphicsItem* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    QString name;
    QString details;
    UniValue nodeData;
};

class VisualDesignerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VisualDesignerDialog(const UniValue& initialActions, QWidget* parent = nullptr);
    ~VisualDesignerDialog();

    UniValue getActions() const { return currentActions; }

private Q_SLOTS:
    void onAddNodeClicked();
    void onRemoveNodeClicked();
    void onApplyClicked();

private:
    QGraphicsView* view;
    QGraphicsScene* scene;
    QListWidget* listNodes;
    QPushButton* btnAdd;
    QPushButton* btnRemove;
    QPushButton* btnApply;
    QPushButton* btnCancel;

    UniValue currentActions;
    QList<DesignerNodeItem*> nodeItems;

    void rebuildScene();
    void setupLayout();
};

#endif // VISUALDESIGNERDIALOG_H

/* *****************************************************************************
Copyright (c) 2016-2021, The Regents of the University of California (Regents).
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of the FreeBSD Project.

REGENTS SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
THE SOFTWARE AND ACCOMPANYING DOCUMENTATION, IF ANY, PROVIDED HEREUNDER IS
PROVIDED "AS IS". REGENTS HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT,
UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

*************************************************************************** */

// Written by: Stevan Gavrilovic, Frank McKenna

#ifdef ARC_GIS
#include "SimCenterMapGraphicsView.h"
#endif

#include "VisualizationWidget.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QGridLayout>
#include <QGroupBox>
#include <QToolButton>
#include <QHeaderView>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QThread>
#include <QTreeView>
#include <QListView>
#include <QUrl>

#include <utility>


VisualizationWidget::VisualizationWidget(QWidget* parent) : SimCenterAppWidget(parent)
{    
    legendView = nullptr;
    this->setContentsMargins(0,0,0,0);

    mainLayout = new QVBoxLayout();
    mainLayout->setMargin(0);

    setAcceptDrops(true);
}


VisualizationWidget::~VisualizationWidget()
{

}

#ifdef ARC_GIS
SimCenterMapGraphicsView* VisualizationWidget::getMapViewWidget() const
{
    return mapViewWidget;
}
#endif


void VisualizationWidget::updateSelectedComponent(const QString& assetType, const QString& uid, const QString& attribute, const QVariant& value)
{

//    auto inputWidget = componentWidgetsMap.value(assetType,nullptr);

//    if(inputWidget)
//    {
//        inputWidget->updateSelectedComponentAttribute(uid,attribute,value);
//    }
//    else
//    {
        this->errorMessage("Could not find the widget for the corresponding asset type");
//    }

}


int VisualizationWidget::getIndexOfVal(const QStringList& headersStr, const QString val)
{
    for(int i =0; i<headersStr.size(); ++i)
    {
        QString headerStr = headersStr.at(i);
        if(headerStr.contains(val, Qt::CaseInsensitive))
            return i;
    }

    return -1;
}


QString VisualizationWidget::createUniqueID(void)
{
    auto id = QUuid::createUuid();

    return id.toString();
}


void VisualizationWidget::takeScreenShot(void)
{

}


void VisualizationWidget::setLegendView(GISLegendView* legndView)
{
    if(legndView == nullptr)
        return;

    legendView = legndView;
}


void VisualizationWidget::hideLegend(void)
{
    legendView->clear();
}


GISLegendView *VisualizationWidget::getLegendView() const
{
    return legendView;
}



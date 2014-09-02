#include "mainwindow.h"

#include <iostream>

#include <QFile>
#include <QFileDialog>
#include <QSettings>
#include <QXmlStreamReader>

#include "primitive.h"
#include "types.h"

using std::cout;
using std::endl;
using InputGen::Application::Primitive;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent)
{
    setupUi(this);
    readSettings();
}


void
MainWindow::readSettings()
{
    QSettings settings("SmartGeometryProcessingGroup", "GlobFitInputGen");
    restoreGeometry(settings.value("MainWindow/geometry").toByteArray());
    restoreState(settings.value("MainWindow/windowState").toByteArray());
}

void
MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings("SmartGeometryProcessingGroup", "GlobFitInputGen");
    settings.setValue("MainWindow/geometry", saveGeometry());
    settings.setValue("MainWindow/windowState", saveState());
    QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow()
{
}

void MainWindow::on_actionLoad_SVG_triggered()
{
    //using InputGen::LinearPrimitive<Scalar>/*::LINE_2D*/;
    //using InputGen::LinearPrimitive<Scalar>::vec;
    using InputGen::Application::Scalar;


    QSettings settings;
    QString defaultPath = settings.value("Path/svgOpen").toString();

    QString path =
    QFileDialog::getOpenFileName(this,
                                 tr("Load svg file"),
                                 defaultPath,
                                 "Scalable Vector Graphics (*.svg)");

    std::vector< Primitive > primitiveSet;

    if (! path.isNull()){
        QFile input(path);
        if (input.open(QIODevice::ReadOnly)) {
            settings.setValue("Path/svgOpen", path);

            QByteArray ba = input.readAll();
            input.close();

            // Read the svg by extracting the paths
            QXmlStreamReader reader(ba);
            while (reader.readNextStartElement()) {
                Primitive::vec globalTranslation(Primitive::vec::Zero());

                while (! ( reader.hasError() || reader.atEnd() ) ) {
                    if (reader.name().compare("g") == 0){
                        QStringList attrTrans (reader.attributes().value("transform").toString().split(QRegExp("[()]")));


                        globalTranslation = Primitive::vec::Zero();

                        while( attrTrans.size() > 0){
                            if (attrTrans.front().compare("translate") == 0 && attrTrans.size() > 1){
                                QStringList coordTr = attrTrans.at(1).split(',');
                                globalTranslation << coordTr.at(0).toDouble(),
                                                     coordTr.at(1).toDouble(),
                                                     Scalar(0.);
                                attrTrans.pop_front();
                                attrTrans.pop_front();
                                cout << "Detect new global translation" << globalTranslation.transpose() << endl;
                            }else
                                attrTrans.pop_front();
                        }

                    }
                    if (reader.name().compare("path") == 0){

                        // Extract path coordinates (attribute d), and split them using ' '
                        QStringList attrList  (reader.attributes().value("d").toString().split(" "));
                        QStringList attrTrans (reader.attributes().value("transform").toString().split(QRegExp("[()]")));

                        // The path is not empty and contains coordinates
                        if( attrList.size() > 1){
                            std::vector< Primitive > lines;
                            Primitive::vec localTranslation(Primitive::vec::Zero());

                            while( attrTrans.size() > 0){
                                if (attrTrans.front().compare("translate") == 0 && attrTrans.size() > 1){
                                    QStringList coordTr = attrTrans.at(1).split(',');
                                    localTranslation << coordTr.at(0).toDouble(),
                                                        coordTr.at(1).toDouble(),
                                                        Scalar(0.);
                                    attrTrans.pop_front();
                                    attrTrans.pop_front();
                                }else
                                    attrTrans.pop_front();
                            }

                            //cout << "Path translation = " << localTranslation.transpose() << endl;

                            if( attrList.front().compare("M") == 0  ||
                                    attrList.front().compare("m") == 0 ){

                                cout << "Reading new set of lines" << endl;

                                attrList.pop_front();


                                // read a list of lines from the file.
                                // At this stage, only the position are extracted
                                QStringList::const_iterator constIterator;
                                for (constIterator = attrList.constBegin(); constIterator != attrList.constEnd();
                                     ++constIterator)
                                    if ((*constIterator).contains(',')){
                                        QStringList coordLists = (*constIterator).split(',');
                                        if (coordLists.size() == 2){
                                            Primitive line (Primitive::LINE_2D);
                                            line.setCoord(Primitive::vec(coordLists.at(0).toDouble(),
                                                                         coordLists.at(1).toDouble(),
                                                                         0));
                                            // in svg, path coordinates are stored relatively
                                            if ( lines.size() != 0)
                                                line.setCoord(line.coord() + lines.back().coord());
                                            lines.push_back(line);
                                        }
                                    }

                                // compute direction of the n-1 lines
                                for (unsigned int i = 0; i < lines.size()-1; i++){
                                    Primitive& l1 = lines[i];
                                    const Primitive& l2 = lines[i+1];
                                    const Primitive::vec dortho= (l2.coord() - l1.coord()).normalized();
                                    const Primitive::vec dir ( dortho(1), - dortho(0), Scalar(0.) );
                                    const Primitive::vec2 dim (-(l2.coord() - l1.coord()).norm(), Scalar(0.));

                                    l1.setDim(dim);
                                    l1.setDir(dir);

                                    cout << "l1=" << l1.coord().transpose() << endl;
                                    cout << "l2=" << l2.coord().transpose() << endl;

                                    cout << l1.coord().transpose() << " "
                                         << l1.dir().transpose() << " "
                                         << l1.dim()(0)
                                         << endl;
                                }

                                // second step of the extraction, we now compute the directions
                                // the path is considered as closed if the last character of the path='z'
                                if (attrList.last().compare("z") != 0) // open path, last line must be removed
                                    lines.pop_back();
                                else{
                                    Primitive& l1 = lines.back();
                                    const Primitive& l2 = lines.front();
                                    const Primitive::vec dortho= (l2.coord() - l1.coord()).normalized();
                                    const Primitive::vec dir ( dortho(1), - dortho(0), Scalar(0.) );
                                    const Primitive::vec2 dim (-(l2.coord() - l1.coord()).norm(), Scalar(0.));

                                    l1.setDim(dim);
                                    l1.setDir(dir);

                                    cout << "l1=" << l1.coord().transpose() << endl;
                                    cout << "l2=" << l2.coord().transpose() << endl;

                                    cout << l1.coord().transpose() << " "
                                         << l1.dir().transpose() << " "
                                         << l1.dim()(0)
                                         << endl;
                                }
                            }
                            // update with transformations
                            for (unsigned int i = 0; i < lines.size(); i++){
                                lines[i].setCoord(lines[i].coord() + localTranslation + globalTranslation);
                            }


                            primitiveSet.insert(primitiveSet.end(), lines.begin(), lines.end());
                        }
                    }
                    reader.readNext();
                }
            }
        }
    }

    _graphicsView->setPrimitives(primitiveSet);
}
#include "metacompute.h"


void MetaCompute::apply(ActionType type, void* args)
{
    m_type = type;
    m_args = args;

    //while(isRunning()) continue;

    start();
}


bool MetaCompute::isConnected() const
{
    return network.isPaired();
}


void MetaCompute::run()
{
    switch (m_type)
    {
    case ActionType::PREVIEW_TREE_ITEMS:
    {
        auto args = (ArgPreviewTreeItems*)m_args;
        args->ret_items = getPreviewTreeItems(args->detective, args->path);
    } break;

    case ActionType::CONNECT_TREE_ITEMS:
    {
        auto args = (ArgConnectTreeItems*)m_args;
        args->ret_items = getConnectTreeItems(args->id, args->hookedList);
    } break;

    case ActionType::HOOK_PREVIEW:
    {
        auto args = (ArgHookPreview*)m_args;
        args->success = hookPreview(*args);
    } break;

    case ActionType::HOOK_FUNCTION:
    {
        auto args = (ArgHookFunction*)m_args;
        args->success = hookFunction(*args);
    } break;

    case ActionType::UNHOOK_FUNCTION:
    {
        auto args = (ArgHookFunction*)m_args;
        args->success = unhookFunction(*args);
    } break;

    default:
    {
        m_type = ActionType::INVALID;
    }
    }

    emit functionReady(m_type, m_args);
}

void MetaCompute::connectNetwork(int id){
    // Fermer la dernière connexion
    if (id != Network::getProcId()) {
        if (Network::isStarted()) {
            Network::close();
        }

        // Injecter le dll dans le processus
        Injector::apply(id, "D:\\Source\\Qt\\build-ProcessAnalyser-Desktop_Qt_6_1_3_MSVC2019_64bit-Debug\\debug\\injection\\ProcessAnalyserDll.dll");

        // Démarrier le serveu
        emit log("");
        emit log("Starting local tcp server for communication with dll.");
        Network::startServer();
        emit log("Server started on port " + Network::getPort() + ".");

        // Installer de connexion avec dll injecté
        Network::accept(id);
    }
}


QList<QTreeWidgetItem*> MetaCompute::getPreviewTreeItems(Detective& detective, QString path)
{
    // Trouver tout les importations
    QList<ModuleObject> libs = detective.getStaticImport(path);

    int funcTotalCount(0);
    QList<QTreeWidgetItem*> items;

    for (int i(0); i < libs.size(); ++i)
    {
        // Créer une ligne pour l'arborescence dll
        auto item = new QTreeWidgetItem;
        item->setData(0, Qt::DisplayRole, QVariant::fromValue(libs[i].getName()));
        item->setData(1, Qt::DisplayRole, QVariant::fromValue(libs[i].getPath()));
        item->setData(2, Qt::DisplayRole, QVariant::fromValue(QString("Bibliothèque")));

        // Définir l'icône
        QIcon rowIcon;
        if (libs[i].isLocal()) {
            rowIcon.addFile(":/Libs/Resources/Lib_NotFound.png");
        } else if (libs[i].isVirtual()) {
            rowIcon.addFile(":/Libs/Resources/Lib_Virtual.png");
        } else {
            rowIcon.addFile(":/Libs/Resources/Lib_OK.png");
        }
        item->setIcon(0, rowIcon);

        // Arrêter si la dll est virtuel,
        // car il contient les fonctions de se mise en oeuvre réelle
        if (libs[i].isVirtual()) {
            items << item;
            continue;
        }

        // TABLEAUX D'EXPORTATION //
        QStringList funcs = detective.getStaticExport(libs[i]);
        if (!libs[i].isLocal() && !libs[i].exports()) {
            item->setIcon(0, QIcon(":/Libs/Resources/Lib_NoExport.png"));
        }

        QList<QTreeWidgetItem*> subItems;
        for (int j(0); j < funcs.size(); ++j)
        {
            auto subItem = new QTreeWidgetItem;
            subItem->setData(0, Qt::DisplayRole, QVariant::fromValue(funcs[j]));
            subItem->setData(2, Qt::DisplayRole, QVariant::fromValue(QString("Fonction")));

            subItems << subItem;
        }

        item->addChildren(subItems);
        items << item;

        funcTotalCount += funcs.size();
    }

    return items;
}


QList<QTreeWidgetItem*> MetaCompute::getConnectTreeItems(int id, QList<QString>& hookedList)
{
    connectNetwork(id);

    // Demande le liste des dépendances
    int counter(0);
    QString gotData;
    QTreeWidgetItem* item = nullptr;
    QList<QTreeWidgetItem*> subItems;
    QList<QTreeWidgetItem*> items;
    Network::sendData(QString() + char(Network::GET_RT_DEPS));

    // Get number of deps
    if (Network::recvData(gotData) <= 0)
        throw "Fuck your connection x2";
    if (gotData[0] != QChar(Network::NUMBER))
        throw "Fuck your algo";
    gotData.remove(0, 1);
    emit progressMaxUpdated(gotData.toInt());

    char next = Network::NEXT_DATA;
    Network::sendData(&next);

    while (true)
    {
        Network::recvData(gotData);
        if (gotData[0] == QChar(Network::SUCC_FINISHED))
            break;

        // Si nous reçu le nom de la bibliothèque
        if (gotData[0].toLatin1() == Network::LIBRARY)
        {
            if (item) {
                item->addChildren(subItems);
                items << item;
                emit progressUpdated(++counter);
            }
            item = new QTreeWidgetItem;

            gotData.remove(0, 1);
            auto parts = gotData.split("\1");
            QString gotName = Helper::getNameFromPath(parts[0]);

            // Créer une ligne pour l'arborescence dll
            item->setData(0, Qt::DisplayRole, QVariant::fromValue(gotName));
            item->setData(1, Qt::DisplayRole, QVariant::fromValue(parts[0]));
            item->setData(2, Qt::DisplayRole, QVariant::fromValue(QString("Bibliothèque")));

            // Définir l'icône
            QIcon rowIcon;
            if (parts[1].toUInt() & 1) { // Is local
                rowIcon.addFile(":/Libs/Resources/Lib_NotFound.png");
            } else if (parts[1].toUInt() & 2) { // Is virtual
                rowIcon.addFile(":/Libs/Resources/Lib_Virtual.png");
            } else {
                rowIcon.addFile(":/Libs/Resources/Lib_OK.png");
            }
            item->setIcon(0, rowIcon);
        }
        // Si nous reçu le nom de la fonction
        else if (gotData[0].toLatin1() == Network::FUNCTION)
        {
            gotData.remove(0, 1);
            auto parts = gotData.split("\1");

            for (int i(0); i < parts.size() - 1; i += 2)
            {
                auto subItem = new QTreeWidgetItem;
                if(hookedList.contains(item->text(0) + '\2' + parts[i])){
                    subItem->setForeground(0, QBrush(Qt::red));
                }
                subItem->setData(0, Qt::DisplayRole, QVariant::fromValue(parts[i + 0]));
                subItem->setData(1, Qt::DisplayRole, QVariant::fromValue("0x" + parts[i + 1]));
                subItem->setData(2, Qt::DisplayRole, QVariant::fromValue(QString("Fonction")));

                subItems << subItem;
            }
        }

        Network::sendData(&next);
    }

    // Dernier addition
    if (item) {
        item->addChildren(subItems);
        items << item;
    }

    return items;
}

bool MetaCompute::hookPreview(ArgHookPreview& args)
{
    connectNetwork(args.id);

    QString gotData;
    Network::sendData(QString() + char(Network::HOOK_PREVIEW));

    char next = Network::NEXT_DATA;
    Network::sendData(&next);

    qDebug() << args.injection_data;
    Network::sendData(args.injection_data);

    Network::recvData(gotData);
    qDebug() << gotData;
    args.overw_insts = gotData;

    Network::recvData(gotData);
    if (gotData[0] == QChar(Network::SUCC_FINISHED)) return true;

    return false;
}

bool MetaCompute::hookFunction(ArgHookFunction& args){
    connectNetwork(args.id);

    QString gotData;
    Network::sendData(QString() + char(Network::HOOK_FUNC));

    char next = Network::NEXT_DATA;
    Network::sendData(&next);

    Network::sendData(args.injection_data);

    Network::recvData(gotData);
    for(auto& sym : gotData){
        qDebug() << Qt::hex << sym;
    }

    args.overw_bytes = gotData;

    Network::recvData(gotData);
    if (gotData[0] == QChar(Network::SUCC_FINISHED)) return true;
    return false;
}

bool MetaCompute::unhookFunction(ArgHookFunction& args){
    connectNetwork(args.id);

    QString gotData;
    Network::sendData(QString() + char(Network::UNHOOK_FUNC));

    char next = Network::NEXT_DATA;
    Network::sendData(&next);

    qDebug() << "OVERWRITTEN BYTES:" << args.overw_bytes;
    Network::sendData(args.injection_data);

    Network::sendData(args.overw_bytes);

    Network::recvData(gotData);
    qDebug() << gotData;

    Network::recvData(gotData);
    qDebug() << Qt::hex << gotData;

    Network::recvData(gotData);
    if (gotData[0] == QChar(Network::SUCC_FINISHED)) return true;
    return false;
}




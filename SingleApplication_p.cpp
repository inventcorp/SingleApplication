// The MIT License (MIT)
//
// Copyright (c) Itay Grudev 2015 - 2018
// Copyright (c) Ildar Gilmanov 2020
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

//
//  W A R N I N G !!!
//  -----------------
//
// This file is not part of the SingleApplication API. It is used purely as an
// implementation detail. This header file may change from version to
// version without notice, or may even be removed.
//

#include <cstdlib>
#include <cstddef>
#include <array>

#include <QDir>
#include <QByteArray>
#include <QDataStream>
#include <QCryptographicHash>
#include <QLocalServer>
#include <QLocalSocket>

#include "SingleApplication.h"
#include "SingleApplication_p.h"

#ifdef Q_OS_UNIX
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

#ifdef Q_OS_WIN
#include <Windows.h>
#include <lmcons.h>
#endif

SingleApplicationPrivate::SingleApplicationPrivate(SingleApplication *parent)
    : q_ptr(parent)
{
}

SingleApplicationPrivate::~SingleApplicationPrivate()
{
    if (m_socket)
    {
        m_socket->close();
        delete m_socket;
    }

    m_memory->lock();
    InstancesInfo *instanceInfo = static_cast<InstancesInfo*>(m_memory->data());

    if (m_server)
    {
        m_server->close();
        delete m_server;

        instanceInfo->m_primary = false;
        instanceInfo->m_primaryPid = -1;
        instanceInfo->m_primaryUser[0] =  '\0';
        instanceInfo->m_checksum = blockChecksum();
    }

    m_memory->unlock();

    delete m_memory;
}

QByteArray SingleApplicationPrivate::getUsername()
{
#ifdef Q_OS_WIN
    std::array<wchar_t, UNLEN + 1> username = {};
    DWORD usernameLength = static_cast<DWORD>(username.size());

    if (GetUserNameW(username.data(), &usernameLength))
    {
        return QString::fromWCharArray(username.data()).toUtf8();
    }

    return qgetenv("USERNAME");
#endif

#ifdef Q_OS_UNIX
    QByteArray username;
    uid_t uid = geteuid();
    struct passwd *pw = getpwuid(uid);

    if (pw)
    {
        username = pw->pw_name;
    }

    if (username.isEmpty())
    {
        username = qgetenv("USER");
    }

    return username;
#endif
}

void SingleApplicationPrivate::generateBlockServerName()
{
    QCryptographicHash appData(QCryptographicHash::Sha256);

    appData.addData("SingleApplication", 17);
    appData.addData(SingleApplication::Application::applicationName().toUtf8());
    appData.addData(SingleApplication::Application::organizationName().toUtf8());
    appData.addData(SingleApplication::Application::organizationDomain().toUtf8());

    if (!m_options.testFlag(SingleApplication::Mode::ExcludeAppVersion))
    {
        appData.addData(SingleApplication::Application::applicationVersion().toUtf8());
    }

    if (!m_options.testFlag(SingleApplication::Mode::ExcludeAppPath))
    {
#ifdef Q_OS_WIN
        appData.addData(SingleApplication::Application::applicationFilePath().toLower().toUtf8());
#else
        appData.addData(SingleApplication::Application::applicationFilePath().toUtf8());
#endif
    }

    // User level block requires a user specific data in the hash
    if (m_options.testFlag(SingleApplication::Mode::User))
    {
        appData.addData(getUsername());
    }

    // Replace the backslash in RFC 2045 Base64 [a-zA-Z0-9+/=] to comply with
    // server naming requirements.
    m_blockServerName = appData.result().toBase64().replace('/', '_');
}

void SingleApplicationPrivate::initializeMemoryBlock()
{
    InstancesInfo *instanceInfo = static_cast<InstancesInfo*>(m_memory->data());

    instanceInfo->m_primary = false;
    instanceInfo->m_secondary = 0;
    instanceInfo->m_primaryPid = -1;
    instanceInfo->m_primaryUser[0] =  '\0';
    instanceInfo->m_checksum = blockChecksum();
}

void SingleApplicationPrivate::startPrimary()
{
    Q_Q(SingleApplication);

    // Successful creation means that no main process exists
    // So we start a QLocalServer to listen for connections
    QLocalServer::removeServer(m_blockServerName);
    m_server = new QLocalServer();

    // Restrict access to the socket according to the
    // SingleApplication::Mode::User flag on User level or no restrictions
    if (m_options.testFlag(SingleApplication::Mode::User))
    {
        m_server->setSocketOptions(QLocalServer::UserAccessOption);
    }
    else
    {
        m_server->setSocketOptions(QLocalServer::WorldAccessOption);
    }

    m_server->listen(m_blockServerName);

    connect(m_server, &QLocalServer::newConnection,
            this, &SingleApplicationPrivate::onConnectionEstablished);

    // Reset the number of connections
    InstancesInfo *instanceInfo = static_cast<InstancesInfo*>(m_memory->data());

    instanceInfo->m_primary = true;
    instanceInfo->m_primaryPid = q->applicationPid();

    const QByteArray username = getUsername();
    const int usernameSize = qMin(username.size(), InstancesInfo::primaryUserSize - 1);

    strncpy_s(instanceInfo->m_primaryUser, InstancesInfo::primaryUserSize,
              username.data(), usernameSize);

    instanceInfo->m_primaryUser[usernameSize] = '\0';
    instanceInfo->m_checksum = blockChecksum();

    m_instanceNumber = 0;
}

void SingleApplicationPrivate::startSecondary()
{
}

void SingleApplicationPrivate::connectToPrimary(std::chrono::milliseconds timeout,
                                                ConnectionType connectionType)
{
    if (!m_socket)
    {
        m_socket = new QLocalSocket();
    }

    if (m_socket->state() == QLocalSocket::ConnectedState)
    {
        return;
    }

    if (m_socket->state() == QLocalSocket::UnconnectedState
            || m_socket->state() == QLocalSocket::ClosingState)
    {
        m_socket->connectToServer(m_blockServerName);
    }

    // Wait for being connected
    if (m_socket->state() == QLocalSocket::ConnectingState)
    {
        m_socket->waitForConnected(timeout.count());
    }

    // Initialisation message according to the SingleApplication protocol.
    // Notify the parent that a new instance had been started
    if (m_socket->state() == QLocalSocket::ConnectedState)
    {
        QByteArray initMessage;
        QDataStream writeStream(&initMessage, QIODevice::WriteOnly);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
        writeStream.setVersion(QDataStream::Qt_5_6);
#endif

        writeStream << m_blockServerName.toLatin1();
        writeStream << static_cast<quint8>(connectionType);
        writeStream << m_instanceNumber;

        const quint16 checksum = qChecksum(initMessage.constData(),
                                           static_cast<quint32>(initMessage.length()));
        writeStream << checksum;

        // The header indicates the message length that follows
        QByteArray header;
        QDataStream headerStream(&header, QIODevice::WriteOnly);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
        headerStream.setVersion(QDataStream::Qt_5_6);
#endif

        headerStream << static_cast<quint64>(initMessage.length());

        m_socket->write(header);
        m_socket->write(initMessage);
        m_socket->flush();

        //TODO: future: QAbstractSocket::waitForBytesWritten() method may fail randomly on Windows
        m_socket->waitForBytesWritten(timeout.count());
    }
}

quint16 SingleApplicationPrivate::blockChecksum()
{
    return qChecksum(static_cast<const char*>(m_memory->data()),
                     offsetof(InstancesInfo, m_checksum));
}

qint64 SingleApplicationPrivate::primaryPid()
{
    m_memory->lock();

    InstancesInfo *instanceInfo = static_cast<InstancesInfo*>(m_memory->data());
    const qint64 pid = instanceInfo->m_primaryPid;

    m_memory->unlock();

    return pid;
}

QString SingleApplicationPrivate::primaryUser()
{
    m_memory->lock();

    InstancesInfo *instanceInfo = static_cast<InstancesInfo*>(m_memory->data());
    const QByteArray username = instanceInfo->m_primaryUser;

    m_memory->unlock();

    return QString::fromUtf8(username);
}

void SingleApplicationPrivate::onConnectionEstablished()
{
    QLocalSocket * const nextSocket = m_server->nextPendingConnection();

    if (!nextSocket)
    {
        return;
    }

    m_connectionMap.insert(nextSocket, ConnectionInfo());

    connect(nextSocket, &QLocalSocket::aboutToClose, this, [nextSocket, this] {
        const ConnectionInfo &info = m_connectionMap[nextSocket];
        onClientConnectionClosed(nextSocket, info.m_instanceId);
    });

    connect(nextSocket, &QLocalSocket::disconnected, this, [nextSocket, this] {
        m_connectionMap.remove(nextSocket);
        nextSocket->deleteLater();
    });

    connect(nextSocket, &QLocalSocket::readyRead, this, [nextSocket, this] {
        const ConnectionInfo &info = m_connectionMap[nextSocket];

        switch(info.m_stage) {
        case ConnectionStage::Header:
            readInitMessageHeader(nextSocket);
            break;
        case ConnectionStage::Body:
            readInitMessageBody(nextSocket);
            break;
        case ConnectionStage::Connected:
            onDataAvailable(nextSocket, info.m_instanceId);
            break;
        default:
            break;
        };
    });
}

void SingleApplicationPrivate::readInitMessageHeader(QLocalSocket *socket)
{
    if (!m_connectionMap.contains(socket))
    {
        return;
    }

    if (socket->bytesAvailable() < static_cast<qint64>(sizeof(quint64)))
    {
        return;
    }

    QDataStream headerStream(socket);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
    headerStream.setVersion(QDataStream::Qt_5_6);
#endif

    // Read the header to know the message length
    quint64 messageLength = 0;
    headerStream >> messageLength;

    ConnectionInfo &info = m_connectionMap[socket];
    info.m_stage = ConnectionStage::Body;
    info.m_messageLength = messageLength;

    if (socket->bytesAvailable() >= static_cast<qint64>(messageLength))
    {
        readInitMessageBody(socket);
    }
}

void SingleApplicationPrivate::readInitMessageBody(QLocalSocket *socket)
{
    Q_Q(SingleApplication);

    if (!m_connectionMap.contains(socket))
    {
        return;
    }

    ConnectionInfo &info = m_connectionMap[socket];

    if (socket->bytesAvailable() < static_cast<qint64>(info.m_messageLength))
    {
        return;
    }

    // Read the message body
    const QByteArray messageBytes = socket->read(info.m_messageLength);
    QDataStream readStream(messageBytes);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
    readStream.setVersion(QDataStream::Qt_5_6);
#endif

    // Server name
    QByteArray latin1Name;
    readStream >> latin1Name;

    // Connection type
    quint8 connectionTypeValue = 0;
    readStream >> connectionTypeValue;
    ConnectionType connectionType = static_cast<ConnectionType>(connectionTypeValue);

    // Instance id
    quint32 instanceId = 0;
    readStream >> instanceId;

    // Checksum
    quint16 messageChecksum = 0;
    readStream >> messageChecksum;

    const quint16 actualChecksum = qChecksum(messageBytes.constData(),
                                             static_cast<quint32>(messageBytes.length() - sizeof(quint16)));

    const bool isValid = readStream.status() == QDataStream::Ok
            && QLatin1String(latin1Name) == m_blockServerName
            && messageChecksum == actualChecksum;

    if (!isValid)
    {
        socket->close();
        return;
    }

    info.m_instanceId = instanceId;
    info.m_stage = ConnectionStage::Connected;

    if (connectionType == ConnectionType::NewInstance
            || (connectionType == ConnectionType::SecondaryInstance
                && m_options.testFlag(SingleApplication::Mode::SecondaryNotification)))
    {
        //TODO: future: add instanceStarted() signal to SingleApplicationPrivate class
        emit q->instanceStarted();
    }

    if (socket->bytesAvailable() > 0)
    {
        onDataAvailable(socket, instanceId);
    }
}

void SingleApplicationPrivate::onDataAvailable(QLocalSocket *socket, quint32 instanceId)
{
    Q_Q(SingleApplication);

    //TODO: future: add messageReceived() signal to SingleApplicationPrivate class
    emit q->messageReceived(instanceId, socket->readAll());
}

void SingleApplicationPrivate::onClientConnectionClosed(QLocalSocket *socket, quint32 instanceId)
{
    if (socket->bytesAvailable() > 0)
    {
        onDataAvailable(socket, instanceId);
    }
}

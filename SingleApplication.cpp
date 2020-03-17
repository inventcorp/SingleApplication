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

#include <QElapsedTimer>
#include <QThread>
#include <QByteArray>
#include <QSharedMemory>

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
#include <QRandomGenerator>
#else
#include <QDateTime>
#endif

#include "SingleApplication.h"
#include "SingleApplication_p.h"

SingleApplication::SingleApplication(int &argc,
                                     char *argv[],
                                     bool allowSecondary,
                                     Options options,
                                     std::chrono::milliseconds timeout)
    : Application(argc, argv),
      d_ptr(new SingleApplicationPrivate(this))
{
    Q_D(SingleApplication);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // On Android and iOS since the library is not supported fallback to
    // standard QApplication behaviour by simply returning at this point.
    qWarning() << "SingleApplication is not supported on Android and iOS systems";
    return;
#endif

    connect(d, &SingleApplicationPrivate::instanceStarted,
            this, &SingleApplication::instanceStarted);

    connect(d, &SingleApplicationPrivate::messageReceived,
            this, &SingleApplication::messageReceived);

    // Store the current mode of the program
    d->m_options = options;

    // Generating an application ID used for identifying the shared memory
    // block and QLocalServer
    d->generateBlockServerName();

#ifdef Q_OS_UNIX
    // By explicitly attaching it and then deleting it we make sure that the
    // memory is deleted even after the process has crashed on Unix.
    d->memory = new QSharedMemory(d->blockServerName);
    d->memory->attach();
    delete d->memory;
#endif

    // Guarantee thread safe behaviour with a shared memory block
    d->m_memory = new QSharedMemory(d->m_blockServerName);

    // Create a shared memory block
    if(d->m_memory->create(sizeof(InstancesInfo)))
    {
        // Initialize the shared memory block
        d->m_memory->lock();
        d->initializeMemoryBlock();
        d->m_memory->unlock();
    }
    else
    {
        // Attempt to attach to the memory segment
        if(!d->m_memory->attach())
        {
            qCritical() << "SingleApplication: Unable to attach to shared memory block."
                        << d->m_memory->errorString();

            delete d;
            ::exit(EXIT_FAILURE);
        }
    }

    InstancesInfo *instanceInfo = static_cast<InstancesInfo*>(d->m_memory->data());
    QElapsedTimer timer;
    timer.start();

    // Make sure the shared memory block is initialised and in consistent state
    while (true)
    {
        d->m_memory->lock();

        if (d->blockChecksum() == instanceInfo->m_checksum)
        {
            break;
        }

        if (timer.elapsed() > 5000)
        {
            qWarning() << "SingleApplication: Shared memory block has been in an inconsistent state from more than 5s. Assuming primary instance failure.";
            d->initializeMemoryBlock();
        }

        d->m_memory->unlock();

        // Random sleep here limits the probability of a collision between two racing apps
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        QThread::sleep(QRandomGenerator::global()->bounded(8u, 18u));
#else
        qsrand(QDateTime::currentMSecsSinceEpoch() % std::numeric_limits<uint>::max());
        QThread::sleep(8 + static_cast<unsigned long>(static_cast<float>(qrand()) / RAND_MAX * 10));
#endif
    }

    if (!instanceInfo->m_primary)
    {
        d->startPrimary();
        d->m_memory->unlock();

        return;
    }

    // Check if another instance can be started
    if (allowSecondary)
    {
        instanceInfo->m_secondary += 1;
        instanceInfo->m_checksum = d->blockChecksum();
        d->m_instanceNumber = instanceInfo->m_secondary;
        d->startSecondary();

        if (d->m_options.testFlag(Mode::SecondaryNotification))
        {
            d->connectToPrimary(timeout,
                                SingleApplicationPrivate::ConnectionType::SecondaryInstance);
        }

        d->m_memory->unlock();
        return;
    }

    d->m_memory->unlock();

    d->connectToPrimary(timeout, SingleApplicationPrivate::ConnectionType::NewInstance);

    delete d;

    ::exit(EXIT_SUCCESS);
}

SingleApplication::~SingleApplication()
{
    Q_D(SingleApplication);

    delete d;
}

bool SingleApplication::isPrimary() const
{
    Q_D(const SingleApplication);

    return d->m_server != nullptr;
}

bool SingleApplication::isSecondary() const
{
    Q_D(const SingleApplication);

    return d->m_server == nullptr;
}

quint32 SingleApplication::instanceId() const
{
    Q_D(const SingleApplication);

    return d->m_instanceNumber;
}

qint64 SingleApplication::primaryPid()
{
    Q_D(SingleApplication);

    return d->primaryPid();
}

QString SingleApplication::primaryUser()
{
    Q_D(SingleApplication);

    return d->primaryUser();
}

bool SingleApplication::sendMessage(const QByteArray &message, std::chrono::milliseconds timeout)
{
    Q_D(SingleApplication);

    // Nobody to connect to
    if (isPrimary())
    {
        return false;
    }

    // Make sure the socket is connected
    d->connectToPrimary(timeout, SingleApplicationPrivate::ConnectionType::Reconnect);

    d->m_socket->write(message);

    //TODO: future: QAbstractSocket::waitForBytesWritten() method may fail randomly on Windows
    const bool dataWritten = d->m_socket->waitForBytesWritten(timeout.count());

    d->m_socket->flush();

    return dataWritten;
}

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
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // On Android and iOS since the library is not supported fallback to
    // standard QApplication behaviour by simply returning at this point.
    qWarning() << "SingleApplication is not supported on Android and iOS systems";
    return;
#else
    Q_D(SingleApplication);

    connect(d, &SingleApplicationPrivate::instanceStarted,
            this, &SingleApplication::instanceStarted);

    connect(d, &SingleApplicationPrivate::messageReceived,
            this, &SingleApplication::messageReceived);

    if (!d->init(allowSecondary, options, timeout))
    {
        delete d;
        ::exit(EXIT_FAILURE);
    }
#endif
}

bool SingleApplication::isPrimary() const
{
    Q_D(const SingleApplication);

    return d->isPrimary();
}

bool SingleApplication::isSecondary() const
{
    Q_D(const SingleApplication);

    return d->isSecondary();
}

quint32 SingleApplication::instanceId() const
{
    Q_D(const SingleApplication);

    return d->instanceId();
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

    return d->sendMessage(message, timeout);
}

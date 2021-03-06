/*
    Copyright (C) 2018 Aseman Team
    http://aseman.co

    TelegramStats is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    TelegramStats is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define UPLOAD_PACKET (16*1024)

#include "stdummybuffer.h"
#include "stuploader.h"

#include <QDebug>
#include <QDateTime>
#include <QTimer>
#include <QTcpSocket>

class STUploader::Private
{
public:
    QSet<QTcpSocket*> sockets;
    STClient *client;

    QDateTime startTime;
    QDateTime readyTime;
    QDateTime finishTime;
    qint64 totalBytes;

    QString error;
    STUploader::Status status;
};

STUploader::STUploader(STClient *client, QObject *parent) :
    QObject(parent)
{
    p = new Private;
    p->totalBytes = 0;
    p->status = Idle;
    p->client = client;
}

qint32 STUploader::delayTime() const
{
    if(p->startTime == p->readyTime)
        return -1;

    return p->startTime.msecsTo(p->readyTime);
}

qint32 STUploader::uploadTime() const
{
    if(p->readyTime == p->finishTime)
        return -1;

    return p->readyTime.msecsTo(p->finishTime);
}

qint32 STUploader::uploadBytes() const
{
    return p->totalBytes;
}

QString STUploader::error() const
{
    return p->error;
}

void STUploader::setError(const QString &error)
{
    if(p->error == error)
        return;

    p->error = error;
    Q_EMIT errorChanged();
}

STUploader::Status STUploader::status() const
{
    return p->status;
}

void STUploader::setStatus(STUploader::Status status)
{
    if(p->status >= status)
        return;

    p->status = status;
    Q_EMIT statusChanged();
}

void STUploader::start(const STServerItem &server, qint32 timeout, qint32 threads)
{
    for(QTcpSocket *socket: p->sockets)
        socket->deleteLater();
    p->sockets.clear();

    QString hostStr = server.host();
    qint32 idx = hostStr.indexOf( QStringLiteral(":") );
    if(idx < 0)
    {
        setError( QStringLiteral("Invalid server") );
        return;
    }

    QString host = hostStr.left(idx);
    qint32 port = hostStr.mid(idx+1).toInt();

    p->totalBytes = 0;
    p->startTime = QDateTime::currentDateTime();
    p->readyTime = p->startTime;
    p->finishTime = p->startTime;

    for(qint32 i=0; i<threads; i++)
    {
        QTcpSocket *socket = new QTcpSocket(this);
        STDummyBuffer *buffer = new STDummyBuffer(1000000000, socket);
        buffer->open(STDummyBuffer::ReadOnly);

        QTimer *timer = new QTimer(socket);
        timer->setInterval(timeout);
        timer->setSingleShot(true);

        setStatus(Connecting);
        connect(socket, &QTcpSocket::connected, this, [this, socket, buffer](){
            if(p->readyTime != p->startTime)
                p->readyTime = QDateTime::currentDateTime();

            socket->write("UPLOAD 1000000000\n");
            socket->write(buffer->read(UPLOAD_PACKET));
            setStatus(Requesting);
            Q_EMIT delayTimeChanged();
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket](){
            socket->deleteLater();
        });
        connect(socket, &QTcpSocket::bytesWritten, this, [this, socket, buffer](qint64 bytes){
            p->totalBytes += bytes;
            socket->write(buffer->read(UPLOAD_PACKET));

            p->finishTime = QDateTime::currentDateTime();
            setStatus(Uploading);
            Q_EMIT uploadTimeChanged();
            Q_EMIT uploadBytesChanged();
        });
        connect(socket, &QTcpSocket::destroyed, this, [this, socket](){
            p->sockets.remove(socket);
            if(p->sockets.isEmpty())
            {
                setStatus(Finished);
                Q_EMIT finished();
            }
        });
        connect(socket, static_cast<void(QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error), this, [this, socket](){
            setError(socket->errorString());
        });
        connect(timer, &QTimer::timeout, socket, &QTcpSocket::deleteLater);

        p->sockets.insert(socket);

        timer->start();
        socket->connectToHost(host, port);
    }

    Q_EMIT delayTimeChanged();
    Q_EMIT uploadTimeChanged();
    Q_EMIT uploadBytesChanged();
}

STUploader::~STUploader()
{
    delete p;
}

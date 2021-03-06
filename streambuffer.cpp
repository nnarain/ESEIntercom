#include "streambuffer.h"
#include <QMutexLocker>

#include <QDebug>

StreamBuffer::StreamBuffer(QObject *parent) : QBuffer(parent)
{
    _sync = new QMutex(QMutex::Recursive);
    _readPos = 0;
}

qint64 StreamBuffer::writeData(const char *data, qint64 len)
{
    QMutexLocker locker(_sync);

    // get the current position in the buffer
    qint64 currentPos = pos();
    // get the end position of the buffer
    qint64 bufferSize = size();

    // seek to the end
    if(bufferSize == 0)
        seek(0);
    else
        seek(bufferSize - 1);

    // write data to the end of the buffer
    qint64 bytesWritten = QBuffer::writeData(data, len);

    // seek back to the current position
    seek(currentPos);

    return bytesWritten;
}

qint64 StreamBuffer::readData(char *data, qint64 maxlen)
{
    QMutexLocker locker(_sync);

    qint64 currentPos = pos();

    seek(_readPos);

    qint64 bytesRead = QBuffer::readData(data, maxlen);
    _readPos += bytesRead;

    seek(currentPos);

    return bytesRead;
}

void StreamBuffer::removeChunk(qint64 offset)
{
    qint64 remainingBytes = size() - offset;

    if(remainingBytes > 0){
        reset();
        seek(offset);

        QByteArray array = read(remainingBytes);

        resetBuffer(array);
        seek(remainingBytes);
    }
    else{
        resetBuffer(QByteArray());
    }
}

void StreamBuffer::resetBuffer(QByteArray &buffer)
{
    if(!isOpen()){
        reset();
        setData(buffer);
        _readPos = 0;
    }
}

qint64 StreamBuffer::readPosition() const
{
    return _readPos;
}

StreamBuffer::~StreamBuffer()
{
    delete _sync;
}

#include "serialcom.h"
#include <QByteArray>
#include <QDebug>

#include <string>

#include "rlencoding.h"
#include "bitopts.h"

#define Q_HEXSTR(x) QString("%1").arg(x, 0, 16)

SerialCom::SerialCom(QObject *parent) :
    QObject(parent)
{
    _serial = new QSerialPort(this);
    connect(_serial, SIGNAL(readyRead()), this, SLOT(onDataReceived()));

    _header.lSignature = FRAME_SIGNATURE;
    _header.bVersion = 1;

    initQueue(&_queue);
}

bool SerialCom::open(SerialSettings::Settings settings)
{
    _serial->setPortName(settings.portName);
    _serial->setBaudRate(settings.baudrate);
    _serial->setDataBits(settings.databits);
    _serial->setStopBits(settings.stopbits);
    _serial->setParity(settings.parity);
    _serial->setFlowControl(settings.flowcontrol);
    _serial->setReadBufferSize(sizeof(Message));

    _receiveBuffer.open(QIODevice::ReadWrite);

    return _serial->open(QIODevice::ReadWrite);
}

void SerialCom::close()
{
    _serial->close();
}

void SerialCom::onDataReceived()
{
    // store in the cumulative buffer
    _receiveBuffer.write(_serial->readAll());

    // don't process buffer until it has buffered enough data
    if(_receiveBuffer.size() >= READY_READ_SIZE){

        // go to the start of the buffer
        _receiveBuffer.reset();

        // pull the frame header
        FrameHeader header;
        _receiveBuffer.read((char*)&header, sizeof(FrameHeader));

        // check if the signature is valid
        if(header.lSignature == FRAME_SIGNATURE){

            // check if any data was dropped
            if(header.lDataLength < _receiveBuffer.size()){

                // check if its a message
                if(header.lDataLength == sizeof(Message)){

                    // allocate memory for the message
                    Message* m = (Message*) malloc(sizeof(Message));

                    if(m != NULL){
                        // pull the message and place in queue
                        _receiveBuffer.read((char*)m, sizeof(Message));

                        // add to queue and notify the ui
                        enQueue(&_queue, m);
                        emit onQueueUpdate(_queue.size);
                    }
                    else{
                        qDebug() << "Could not allocate memory for a new message :(\n";
                    }

                    // clear the serial buffer
                    resetBuffer(_receiveBuffer);
                }

            }
            else{
                qDebug() << "not enough sent! problem!\n";
                resetBuffer(_receiveBuffer);
            }

        }
        else{
            qDebug() << "Corrupt data!\n";
            resetBuffer(_receiveBuffer);
        }

    }

    // debug
    if(_serial->bytesAvailable() == DEBUG_SERIAL_OUT.length() + sizeof(FrameHeader)){
        QByteArray data = _serial->readAll();
        QString msg(data);

        if(msg == DEBUG_SERIAL_OUT){
            qDebug() << "Debug message recieved" << "\n";
            qDebug() << msg << "\n";
        }
    }

}

void SerialCom::write(QByteArray stringBuffer, uint8_t receiverId)
{
    QBuffer outData;
    outData.open(QIODevice::WriteOnly);

    // fill some header data
    _header.bReceiverId = receiverId;
    _header.bDecodeOpts = bv(MSG_TYPE_TEXT) | bv(COMPRESS_TYPE_RLE) | bv(ENCRYPT_TYPE_NONE);

    // write message data
    Message message;
    message.priority = 1;
    message.msgSeq = 7;
    message.receiverID = receiverId;
    message.senderID = 42;

    memcpy(message.msg, stringBuffer.data(), BUFFER_MAX);

    // RL Encode the data to be sent
    uint8_t* encodedBuffer = (uint8_t*) malloc(sizeof(Message));
    int iEncodeLen = rlencode((uint8_t*)&message, sizeof(Message), encodedBuffer, sizeof(Message), 27);

    _header.lDataLength = iEncodeLen;

    // write framed data to the out Buffer
    outData.write((char*)&_header, sizeof(FrameHeader));
    outData.write((char*)encodedBuffer, iEncodeLen);

    // write out to serial
    outData.close();
    qDebug() << "written bytes: " << _serial->write(outData.buffer());

    free(encodedBuffer);
}

Message* SerialCom::getNextMessageFromQueue()
{
    Message* message = deQueue(&_queue);
    emit onQueueUpdate(_queue.size);
    return message;
}

void SerialCom::resetBuffer(QBuffer& buffer)
{
    if(buffer.isOpen()){
        // seek to start and close
        buffer.reset();
        buffer.close();
        // clear data
        buffer.setData(QByteArray());
        // reopen
        buffer.open(QIODevice::ReadWrite);
    }
    else{
        // open
        buffer.open(QIODevice::ReadWrite);
    }
}

SerialCom::~SerialCom()
{
    delete _serial;
    if(_queue.size > 0) deleteQueue(&_queue);
}

#include "serialcom.h"
#include <QByteArray>
#include <QDebug>

#include <string>

#define Q_HEXSTR(x) QString("%1").arg(x, 0, 16)

typedef struct test{
    int x, y, z;
    int chunk[100];
}Test;

SerialCom::SerialCom(QObject *parent) :
    QObject(parent)
{
    _serial = new QSerialPort(this);
    connect(_serial, SIGNAL(readyRead()), this, SLOT(onDataReceived()));

    _header.lSignature = FRAME_SIGNATURE;
    _header.bPattern = 0x5A;
    _header.bVersion = 1;
    _header.bTBD[0] = 0;

    initQueue(&_queue);

    qDebug() << "sizeof(Message): " << sizeof(Message) << "\n";
    qDebug() << "sizeof(FrameHeader): " << sizeof(FrameHeader) << "\n";
    qDebug() << "Message + FrameHeader: " << sizeof(Message) + sizeof(FrameHeader)  << "\n";
    qDebug() << "Message - FrameHeader:" << sizeof(Message) - sizeof(FrameHeader) << "\n";
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
        _receiveBuffer.seek(0);

        // pull the frame header
        FrameHeader header;
        _receiveBuffer.read((char*)&header, sizeof(FrameHeader));

        // check if the signature is valid
        if(header.lSignature == FRAME_SIGNATURE){

            // check if any data was dropped
            if(header.lDataLength <= _receiveBuffer.size()){

                // check if its a message
                if(header.lDataLength == sizeof(Message)){

                    // allocate memory for the message
                    Message* m = (Message*) malloc(sizeof(Message));

                    if(m != NULL){
                        // pull the message and place in queue
                        _receiveBuffer.read((char*)m, sizeof(Message));
                        enQueue(&_queue, m);
                        emit messageReceived(_queue.size);
                        qDebug() << m->msg << "\n";
                    }
                    else{
                        qDebug() << "Could not allocate memory for a new message :(\n";
                    }
                }

            }
            else{
                qDebug() << "not enough sent! problem!\n";
            }

        }
        else{
            qDebug() << "Corrupt data!\n";
            qDebug() << "available: " << _serial->bytesAvailable() << "\n";
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
    _header.lDataLength = sizeof(Message);

    // frame the data
    outData.write((char*)&_header, sizeof(FrameHeader));

    // write message data
    Message message;
    message.priority = 1;
    message.msgSeq = 7;
    message.receiverID = receiverId;
    message.senderID = 42;

    memcpy(message.msg, stringBuffer.data(), BUFFER_MAX);

    outData.write((char*)&message, sizeof(Message));

    // write out to serial
    outData.close();
    qDebug() << "written bytes: " << _serial->write(outData.buffer());
}


SerialCom::~SerialCom()
{
    delete _serial;
    if(_queue.size > 0) deleteQueue(&_queue);
}

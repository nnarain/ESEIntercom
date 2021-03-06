
/**
    @file serialcom.cpp
    @breif Serial Communication
    @author Natesh Narain
*/

/**
    @file serialcom.cpp
    @breif All communication goes through here
    @author Natesh Narain
*/

#include "serialcom.h"

#include <QByteArray>
#include <QChar>
#include <QTime>

#include <QDebug>

#include <string>
#include <cstdlib>

#include "rlencoding.h"
#include "bitopts.h"

//! Hex String from int
#define Q_HEXSTR(x) QString("%1").arg(x, 0, 16)
//! Pointer to Hex String
#define Q_PTRSTR(x) QString("0x%1").arg((qintptr)x, QT_POINTER_SIZE * 2, 16, QChar('0'));

#define vote(x,y) (x == y)

SerialCom::SerialCom(QObject *parent) : QObject(parent)
{
    _serial = new QSerialPort(this);
    connect(_serial, SIGNAL(readyRead()), this, SLOT(onDataReceived()));

    _isProcessingPacket = false;
    _useHeader = true;
    _checksumDivisor = 16;

    initQueue(&_queue);
    initPhoneBook(&_log);
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
    resetBuffer(_receiveBuffer);
    _isProcessingPacket = false;
}

void SerialCom::onDataReceived()
{
    // store in the cumulative buffer
    _receiveBuffer.write(_serial->readAll());

    if(!_useHeader){
        _receiveBuffer.reset();
        QByteArray bytes = _receiveBuffer.readAll();

        qDebug() << bytes << "\n";

        return ;
    }

    // check if there is data for a packet header
    if(_isProcessingPacket == false && _receiveBuffer.size() >= sizeof(FrameHeader)){
        qDebug() << "Getting packet header";

        // go to the start of the buffer
        _receiveBuffer.reset();

        // read the frame header
        _receiveBuffer.read((char*)&_inHeader, sizeof(FrameHeader));

        // verify the packet is valid
        if(_inHeader.lSignature == FRAME_SIGNATURE && vote(_inHeader.lSignature, _inHeader.lSignature2)){
            // check if the correct station
            qDebug() << "receiver id: " << _inHeader.bReceiverId;
            if(_inHeader.bReceiverId == _stationId || (isBitSet(_inHeader.bDecodeOpts, MSG_TYPE_AUDIO) || isBitSet(_inHeader.bDecodeOpts, MSG_TYPE_AUDIO_STREAM))){
                qDebug() << "Valid header received\n";
                qDebug() << "Will wait for " << _inHeader.lDataLength << " bytes";
                // specify that a packet is now being processed
                _isProcessingPacket = true;
                removeProcessedData(_receiveBuffer, sizeof(FrameHeader));
            }
            else{
                qDebug() << "Data not for this station";
                resetBuffer(_receiveBuffer);
            }
        }
        else{
            resetBuffer(_receiveBuffer);
            qDebug() << "Data discarded, invalid signature";
        }
        qDebug() << "\n";
    }

    // check if a packet is being processed
    if(_isProcessingPacket){

        // check for the number of bytes specified by the header
        if(_receiveBuffer.size() >= _inHeader.lDataLength){
            qDebug() << "Enough data received";

            // move to the front of the buffer for reading
            _receiveBuffer.reset();

            // decrypt the buffer
            if(isBitSet(_inHeader.bDecodeOpts, ENCRYPT_TYPE_XOR)){
                qDebug() << "decrypting";
                // put data into a temporary buffer
                QBuffer tmp;
                QByteArray data = _receiveBuffer.read(_inHeader.lDataLength);
                tmp.setData(data);

                // reset the receive buffer and open tmp buffer
                _receiveBuffer.reset();
                tmp.open(QIODevice::ReadWrite);

                // decrypt the buffer putting content into recieve buffer
                encryptXOR(_receiveBuffer, tmp, _inHeader.bEncryptionKey);
                tmp.close();
                _receiveBuffer.reset();
            }

            // using RLE compression
            if(isBitSet(_inHeader.bDecodeOpts, COMPRESS_TYPE_RLE)){
                qDebug() << "RL Decode";

                // create a buffer for the uncompressed data
                uint8_t* decodeBuffer = (uint8_t*) malloc(_inHeader.lUncompressedLength);

                // get the data from the buffer
                QByteArray bytes = _receiveBuffer.read(_inHeader.lDataLength);
                uint8_t* raw = (uint8_t*) bytes.data();

                //
                if(isBitSet(_inHeader.bDecodeOpts, MSG_TYPE_TEXT)){

                    // uncompress the data
                    rldecode(raw, _inHeader.lDataLength, decodeBuffer, _inHeader.lUncompressedLength, 0x1B);

                    Message* message = (Message*)decodeBuffer;

                    // validate checksum
                    if(message->checksum = checksum((uint8_t*)message, sizeof(Message), _checksumDivisor)){
                        enQueue(&_queue, message);
                        insertIntoPhoneBook(&_log, message);
                        emit onQueueUpdate(_queue.size);
                        qDebug() << message->msg << "\n";
                    }
                }
                else if(isBitSet(_inHeader.bDecodeOpts, MSG_TYPE_AUDIO)){
                    qDebug() << "decode audio broadcast";

                    // uncompress the data
                    rldecode(raw, _inHeader.lDataLength, decodeBuffer, _inHeader.lUncompressedLength, 0xFF);

                    QByteArray audioBuffer;
                    audioBuffer.append((char*)decodeBuffer, _inHeader.lDataLength);

                    emit onAudioReceived(audioBuffer);
                }
                else if(isBitSet(_inHeader.bDecodeOpts, MSG_TYPE_AUDIO_STREAM)){
                    qDebug() << "decode audio stream";

                    // decompress data
                    rldecode(raw, _inHeader.lDataLength, decodeBuffer, _inHeader.lUncompressedLength, 0xFF);

                    QByteArray audioBuffer;
                    audioBuffer.append((char*)decodeBuffer, _inHeader.lDataLength);

                    emit onAudioStreamReceived(audioBuffer);
                }

               // free(decodeBuffer);

            }else{
                qDebug() << "No compression";

                // Uncompressed Text Message
                if(isBitSet(_inHeader.bDecodeOpts, MSG_TYPE_TEXT)){
                    qDebug() << "Uncompressed text";

                    Message* message = (Message*) malloc(sizeof(Message));

                    _receiveBuffer.read((char*)message, sizeof(Message));

                    // validate checksum
                   // if(message->checksum == checksum((uint8_t*)message, sizeof(Message), _checksumDivisor)){
                        enQueue(&_queue, message);
                        emit onQueueUpdate(_queue.size);

                        qDebug() << message->msg << "\n";
                   // }

                }
                // Uncompressed Audio Message
                else if(isBitSet(_inHeader.bDecodeOpts, MSG_TYPE_AUDIO)){
                    qDebug() << "Receive Uncompressed Audio Message";

                    // send the audio buffer to the broadcast player
                    QByteArray audioBuffer = _receiveBuffer.read(_inHeader.lDataLength);
                    emit onAudioReceived(audioBuffer);

                }
                // Uncompressed audio stream
                else if(isBitSet(_inHeader.bDecodeOpts, MSG_TYPE_AUDIO_STREAM)){

                    QByteArray audioBuffer = _receiveBuffer.read(_inHeader.lDataLength);
                    emit onAudioStreamReceived(audioBuffer);

                }
            }

            // finished packet processing
            _isProcessingPacket = false;
            removeProcessedData(_receiveBuffer, _inHeader.lDataLength);
        }

    }

}

void SerialCom::write(QByteArray buffer, uint8_t receiverId, bool useHeader, uint8_t decodeOptions)
{
    qDebug() << "Serial Write";
    QBuffer outData;
    outData.open(QIODevice::ReadWrite);

    FrameHeader outHeader;
    outHeader.lSignature = FRAME_SIGNATURE;
    outHeader.lSignature2 = FRAME_SIGNATURE;
    outHeader.bVersion = 1;
    outHeader.bEncryptionKey = (uint8_t)'Q';

    if(useHeader){
        qDebug() << "Using Framed Data";
        // fill initial header data
        outHeader.bReceiverId = receiverId;
        qDebug() << "send: receiver: " << receiverId;

        outHeader.bDecodeOpts = 0;
        set(outHeader.bDecodeOpts, decodeOptions);

        // handle text message
        if(isBitSet(decodeOptions, MSG_TYPE_TEXT)){
            qDebug() << "Sending Text";

            // copy buffer into the message structure
            Message message;
            memset(message.msg, '\0', BUFFER_MAX);
            message.receiverID = receiverId;
            message.priority = 1;
            message.senderID = rand() % 5;
            message.timestamp = (uint32_t) QDateTime::currentDateTimeUtc().toTime_t();

            memcpy(message.msg, buffer.data(), BUFFER_MAX);

            message.checksum = checksum((uint8_t*)&message, sizeof(Message), _checksumDivisor);

            qDebug() << "send: checksum: " << message.checksum;

            // check if doing compression
            if(isBitSet(decodeOptions, COMPRESS_TYPE_HUFF) || isBitSet(decodeOptions, COMPRESS_TYPE_RLE)){

                // Run length encoding
                if(isBitSet(decodeOptions, COMPRESS_TYPE_RLE)){
                    qDebug() << "RL Encoding";

                    uint8_t* encodedBuffer = (uint8_t*) malloc(sizeof(Message));
                    int iEncodeLen = rlencode((uint8_t*)&message, sizeof(Message), encodedBuffer, sizeof(Message), 27);

                    outHeader.lUncompressedLength = sizeof(Message);
                    outHeader.lDataLength = iEncodeLen;

                    outData.write((char*)&outHeader, sizeof(FrameHeader));

                    if(isBitSet(decodeOptions, ENCRYPT_TYPE_XOR))
                        encryptXOR(outData, (uint8_t*)encodedBuffer, iEncodeLen, outHeader.bEncryptionKey);
                    else
                        outData.write((char*)encodedBuffer, iEncodeLen);

                  //  free(encodedBuffer);

                }

            }
            // no compression of a message
            else{
                qDebug() << "No Compression";
                outHeader.lUncompressedLength = sizeof(Message);
                outHeader.lDataLength = sizeof(Message);

                outData.write((char*)&outHeader, sizeof(FrameHeader));

                if(isBitSet(decodeOptions, ENCRYPT_TYPE_XOR))
                    encryptXOR(outData, (uint8_t*)&message, sizeof(Message), outHeader.bEncryptionKey);
                else
                    outData.write((char*)&message, sizeof(Message));
            }

        }
        // handle audio message
        else if(isBitSet(decodeOptions, MSG_TYPE_AUDIO) || isBitSet(decodeOptions, MSG_TYPE_AUDIO_STREAM)){
            qDebug() << "Send Audio";

            if(isBitSet(decodeOptions, COMPRESS_TYPE_HUFF) || isBitSet(decodeOptions, COMPRESS_TYPE_RLE)){

                if(isBitSet(decodeOptions, COMPRESS_TYPE_RLE)){
                    qDebug() << "RLE compression";

                    int len = buffer.length();

                    uint8_t* encodedBuffer = (uint8_t*) malloc(len * sizeof(uint8_t));
                    int iEncodeLen = rlencode((uint8_t*)buffer.data(), len, encodedBuffer, len, 0xFF);

                    outHeader.lUncompressedLength = len;
                    outHeader.lDataLength = iEncodeLen;

                    qDebug() << "Uncompressed: " << outHeader.lUncompressedLength;
                    qDebug() << "Data Length : " << outHeader.lDataLength;
                    qDebug() << "Compression Ratio: " << ((float)outHeader.lUncompressedLength/(float)outHeader.lDataLength);

                    outData.write((char*)&outHeader, sizeof(FrameHeader));

                    if(isBitSet(decodeOptions, ENCRYPT_TYPE_XOR))
                        encryptXOR(outData, encodedBuffer, iEncodeLen, outHeader.bEncryptionKey);
                    else
                        outData.write((char*)encodedBuffer, iEncodeLen);

                   // free(encodedBuffer);
                }

            }
            // uncompressed audio
            else{
                outHeader.lDataLength = buffer.length();
                outHeader.lUncompressedLength = buffer.length();
                outData.write((char*)&outHeader, sizeof(FrameHeader));
                outData.write(buffer);
            }

        }
    }
    else{
        outData.write(buffer);
    }

    // write out to serial
    qDebug() << "written bytes: " << _serial->write(outData.buffer()) << "\n";
    outData.close();
}

Message* SerialCom::getNextMessageFromQueue()
{
    Message* message = deQueue(&_queue);
    emit onQueueUpdate(_queue.size);
    return message;
}

void SerialCom::encryptXOR(QBuffer& outBuffer, QBuffer &buffer, uint8_t key)
{
    int size = buffer.size();
    int i;

    for(i = 0; i < size; i++){
        uint8_t byte;
        buffer.read((char*)&byte, 1);
        byte ^= key;
        outBuffer.write((const char*)&byte, 1);
    }
}

void SerialCom::encryptXOR(QBuffer &outBuffer, uint8_t *data, int len, uint8_t key)
{
    int i;

    for(i = 0; i < len; ++i){
        uint8_t x = data[i] ^ key;
        outBuffer.write((const char*)&x, 1);
    }
}

uint8_t SerialCom::checksum(uint8_t* bytes, int len, uint8_t divisor)
{
    uint8_t sum = 0;
    int i;

    for(i = 0; i < len; i++){
        sum += bytes[i];
    }

    return sum % divisor;
}

void SerialCom::removeProcessedData(QBuffer& buffer, qint64 offset)
{
    // find the number of bytes after the offset
    qint64 remainingBytes = buffer.size() - offset;

    if(remainingBytes > 0){

        buffer.reset();

        // seek to the start of the chunk that is going to be moved
        buffer.seek(offset);
        // put that chunk in a byte array
        QByteArray array = buffer.read(remainingBytes);

        // clear the buffer and put the array at the start
        resetBuffer(buffer, array);
        // move to the end of the data
        buffer.seek(remainingBytes);
    }
    else{
        resetBuffer(buffer);
    }
}

void SerialCom::resetBuffer(QBuffer& buffer, QByteArray& array)
{
    if(buffer.isOpen()){
        // seek to start and close
        buffer.reset();
        buffer.close();
        // clear data
        buffer.setData(array);
        // reopen
        buffer.open(QIODevice::ReadWrite);
    }
    else{
        // open
        buffer.open(QIODevice::ReadWrite);
    }
}

void SerialCom::resetBuffer(QBuffer& buffer)
{
    resetBuffer(buffer, QByteArray());
}

void SerialCom::setUseHeader(bool use)
{
    _useHeader = use;
}

bool SerialCom::isUsingHeader() const
{
    return _useHeader;
}

PhoneLog* SerialCom::getPhoneLog()
{
    return &_log;
}

void SerialCom::setStationId(int id)
{
    _stationId = id;
}

SerialCom::~SerialCom()
{
    delete _serial;
    if(_queue.size > 0) deleteQueue(&_queue);
}

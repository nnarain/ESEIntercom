
/**
    Main communication interface to RS232

    @author Natesh Narain
*/

#ifndef SERIALCOM_H
#define SERIALCOM_H

#include <cstdint>

#include <QObject>
#include <QtSerialPort/QSerialPort>
#include <QByteArray>
#include <QBuffer>

#include "serialsettings.h"
#include "messagequeue.h"

#define FRAME_SIGNATURE 0xDEADBEEF
#define DEBUG_SERIAL_OUT QString("DEADBEEF")

#define READY_READ_SIZE sizeof(FrameHeader) + sizeof(Message)

#define MSG_TYPE_TEXT         0x00
#define MSG_TYPE_AUDIO        0x01
#define MSG_TYPE_AUDIO_STREAM 0x02

#define ENCRYPT_TYPE_NONE     0x00
#define ENCRYPT_TYPE_XOR      0x01

#define COMPRESS_TYPE_NONE    0x00
#define COMPRESS_TYPE_RLE     0x01
#define COMPRESS_TYPE_HUFF    0x02

//! Packet header for the outgoing data
typedef struct frameHeader{
    uint32_t lSignature;       ///< Signature to verify the packet
    uint32_t lDataLength;      ///< length of data after the header
    uint8_t  bReceiverId;      ///< the id of the receiver
    uint8_t  bVersion;         ///< the header version
    uint8_t  bPattern;         ///< extra verification
    uint8_t  bMessageType;     ///< Audio\Text, (Audio Stream?)
    uint8_t  bEncryptType;     ///< XOR\None
    uint8_t  bCompressionType; ///< RLE\Huffman\None
}FrameHeader;

/**
    Serial communication interface.

    Handle read, write, queuing, framing, etc
*/
class SerialCom : public QObject
{
    Q_OBJECT
public:
    explicit SerialCom(QObject *parent = 0);
    ~SerialCom();

signals:
    /**
        Emmitted when the data in the queue changes

        @param numQueued
            The number of messages in the queue
    */
    void onQueueUpdate(int numQueued);

public slots:
    /**
        Called when there is data to be read from the serial buffer
    */
    void onDataReceived();

public:
    /**
        Opens the serial port

        @param settings
            The Serial settings to configure the serial port with

        @return true for success, false otherwise
    */
    bool open(SerialSettings::Settings settings);

    /**
        Close the serial port
    */
    void close();

    /**
        Write data to the serial port

        @param data
            data to be written to the serial port
    */
    void write(QByteArray data, uint8_t receiverId);

    /**
        @return the next message in the queue
    */
    Message* getNextMessageFromQueue();

private:
    //! serial port access
    QSerialPort* _serial;
    //! serial data buffer
    QBuffer _receiveBuffer;

    //! Packet header
    FrameHeader _header;

    //! queue for the incoming messages
    MessageQueue _queue;

    /**
        Clear the serial buffer
    */
    void resetSerialBuffer();

};

#endif // SERIALCOM_H

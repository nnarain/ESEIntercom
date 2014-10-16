#include "audioplayback.h"
#include <QAudioDeviceInfo>

#include <QDebug>

AudioPlayback::AudioPlayback(QAudioFormat format, QObject *parent) :
    QObject(parent)
{
    QAudioDeviceInfo info = QAudioDeviceInfo::defaultInputDevice();
    _input = new QAudioInput(info, format, this);
    _output = new QAudioOutput(format, this);

    _recording = false;
    _playing = false;
}

void AudioPlayback::record()
{
    qDebug() << "Recording\n";
    _buffer.open(QIODevice::WriteOnly);
    _input->start(&_buffer);
    _recording = true;
}

void AudioPlayback::stopRecording()
{
    qDebug() << "Stop Recording\n";
    _input->stop();
    _buffer.close();
    _recording = false;
}

void AudioPlayback::play()
{
    qDebug() << "Play\n";
    _buffer.open(QIODevice::ReadOnly);
    _buffer.seek(0);
    _output->start(&_buffer);
    _playing = true;
}

void AudioPlayback::stopPlayback()
{
    qDebug() << "Stop Play\n";
    _output->stop();
    _buffer.close();
    _playing = false;
}

bool AudioPlayback::isRecording()
{
    return _recording;
}

bool AudioPlayback::isPlaying()
{
    return _recording;
}

AudioPlayback::~AudioPlayback()
{
    delete _input;
    delete _output;
}
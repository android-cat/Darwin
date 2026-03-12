#pragma once

#include <QVector>
#include <QString>

class WavWriter
{
public:
    static bool writeStereo16(const QString& filePath,
                              const QVector<float>& samplesL,
                              const QVector<float>& samplesR,
                              int sampleRate,
                              QString* errorMessage = nullptr);
};

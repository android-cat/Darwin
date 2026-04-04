#include <QtTest/QtTest>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "Clip.h"
#include "common/AudioFileReader.h"
#include "common/FontManager.h"
#include "common/ThemeManager.h"
#include "common/WavWriter.h"

class CommonTests : public QObject
{
    Q_OBJECT

private slots:
    void wav_writer_and_reader_roundtrip()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString wavPath = tempDir.filePath(QStringLiteral("roundtrip.wav"));
        const QVector<float> samplesL{0.0f, -0.5f, 1.25f, 0.25f};
        const QVector<float> samplesR{0.5f, 0.25f, -1.5f, -0.25f};
        QString errorMessage;

        QVERIFY(WavWriter::writeStereo16(wavPath, samplesL, samplesR, 44100, &errorMessage));
        QVERIFY2(QFileInfo::exists(wavPath), qPrintable(errorMessage));

        const AudioFileData data = AudioFileReader::readFile(wavPath);
        QVERIFY2(data.valid, qPrintable(data.errorMessage));
        QCOMPARE(data.channels, 2);
        QCOMPARE(data.sampleRate, 44100.0);
        QCOMPARE(data.samplesL.size(), samplesL.size());
        QCOMPARE(data.samplesR.size(), samplesR.size());
        QVERIFY(std::abs(data.samplesL.at(1) - samplesL.at(1)) < 0.001f);
        QVERIFY(std::abs(data.samplesL.at(2) - 1.0f) < 0.001f);
        QVERIFY(std::abs(data.samplesR.at(2) + 1.0f) < 0.001f);
        QVERIFY(!data.waveformPreview.isEmpty());

        Clip clip(0, 480);
        QVERIFY(clip.loadAudioFile(wavPath));
        QVERIFY(clip.isAudioClip());
        QCOMPARE(clip.audioFilePath(), wavPath);
        QCOMPARE(clip.audioSamplesL().size(), samplesL.size());
        QVERIFY(!clip.waveformPreview().isEmpty());
    }

    void wav_writer_reports_errors_and_reader_rejects_invalid_input()
    {
        QString errorMessage;
        QVERIFY(!WavWriter::writeStereo16(QStringLiteral("/tmp/should_not_exist.wav"),
                                          {0.0f, 0.1f}, {0.0f}, 44100, &errorMessage));
        QVERIFY(!errorMessage.isEmpty());

        const AudioFileData unsupported = AudioFileReader::readFile(QStringLiteral("voice.ogg"));
        QVERIFY(!unsupported.valid);
        QVERIFY(unsupported.errorMessage.contains(QStringLiteral("未対応")));

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString invalidWavPath = tempDir.filePath(QStringLiteral("invalid.wav"));

        QFile invalidFile(invalidWavPath);
        QVERIFY(invalidFile.open(QIODevice::WriteOnly));
        invalidFile.write("not a wav");
        invalidFile.close();

        const AudioFileData invalid = AudioFileReader::readFile(invalidWavPath);
        QVERIFY(!invalid.valid);
        QVERIFY(!invalid.errorMessage.isEmpty());
    }

    void audio_file_reader_preview_and_support_detection()
    {
        QVERIFY(AudioFileReader::isSupportedAudioFile(QStringLiteral("a.wav")));
        QVERIFY(AudioFileReader::isSupportedAudioFile(QStringLiteral("a.MP3")));
        QVERIFY(AudioFileReader::isSupportedAudioFile(QStringLiteral("a.m4a")));
        QVERIFY(!AudioFileReader::isSupportedAudioFile(QStringLiteral("a.flac")));

        const QVector<float> preview = AudioFileReader::generateWaveformPreview(
            {0.0f, 0.5f, -0.25f, 1.0f},
            {-0.1f, 0.25f, -0.75f, 0.2f},
            2);
        QCOMPARE(preview.size(), 2);
        QCOMPARE(preview.at(0), 0.5f);
        QCOMPARE(preview.at(1), 1.0f);

        const QVector<float> monoPreview = AudioFileReader::generateWaveformPreview(
            {0.1f, -0.4f, 0.3f, -0.2f},
            {},
            2);
        QCOMPARE(monoPreview.size(), 2);
        QCOMPARE(monoPreview.at(0), 0.4f);
        QCOMPARE(monoPreview.at(1), 0.3f);

        QVERIFY(AudioFileReader::generateWaveformPreview({}, {}, 16).isEmpty());
        QVERIFY(AudioFileReader::generateWaveformPreview({0.1f}, {0.1f}, 0).isEmpty());
    }

    void theme_and_font_manager_public_api_is_consistent()
    {
        auto& themeManager = Darwin::ThemeManager::instance();
        QSignalSpy themeSpy(&themeManager, &Darwin::ThemeManager::themeChanged);

        const bool initialDarkMode = themeManager.isDarkMode();
        const QColor initialBackground = themeManager.backgroundColor();

        themeManager.toggleTheme();
        QCOMPARE(themeManager.isDarkMode(), !initialDarkMode);
        QVERIFY(themeManager.backgroundColor() != initialBackground);

        themeManager.toggleTheme();
        QCOMPARE(themeManager.isDarkMode(), initialDarkMode);
        QCOMPARE(themeSpy.count(), 2);

        auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
        QVERIFY(app != nullptr);

        Darwin::FontManager::configureApplicationFonts(*app);
        QVERIFY(!Darwin::FontManager::uiFontFamily().isEmpty());
        QVERIFY(!Darwin::FontManager::monoFontFamily().isEmpty());
        QVERIFY(Darwin::FontManager::uiFontCss().contains('\''));
        QVERIFY(Darwin::FontManager::monoFontCss().contains(QStringLiteral("monospace")));

        const QFont uiFont = Darwin::FontManager::uiFont(13, QFont::DemiBold);
        const QFont monoFont = Darwin::FontManager::monoFont(12, QFont::Medium);

        QCOMPARE(uiFont.pointSize(), 13);
        QCOMPARE(uiFont.weight(), static_cast<int>(QFont::DemiBold));
        QCOMPARE(monoFont.pointSize(), 12);
        QCOMPARE(monoFont.weight(), static_cast<int>(QFont::Medium));
        QCOMPARE(monoFont.fixedPitch(), true);
    }
};

int runCommonTests(int argc, char** argv)
{
    CommonTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "TestCommon.moc"

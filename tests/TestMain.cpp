#include <QApplication>

int runModelTests(int argc, char** argv);
int runUndoCommandTests(int argc, char** argv);
int runCommonTests(int argc, char** argv);

int main(int argc, char** argv)
{
    // ヘッドレス環境でも起動しやすいよう、未指定時のみ最小プラットフォームを選ぶ。
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "minimal");
    }

    QApplication app(argc, argv);

    int status = 0;
    status |= runModelTests(argc, argv);
    status |= runUndoCommandTests(argc, argv);
    status |= runCommonTests(argc, argv);
    return status;
}

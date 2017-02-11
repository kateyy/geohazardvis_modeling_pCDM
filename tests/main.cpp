#include <QCommandLineParser>
#include <QApplication>

#include <gtest/gtest.h>


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    
    QCommandLineParser cmdParser;
    const QCommandLineOption waitAfterFinishedOption("waitAfterFinished");
    cmdParser.addOption(waitAfterFinishedOption);
    cmdParser.parse(app.arguments());

    ::testing::InitGoogleTest(&argc, argv);

    auto result = RUN_ALL_TESTS();

    if (cmdParser.isSet(waitAfterFinishedOption))
    {
        getchar();
    }

    return result;
}

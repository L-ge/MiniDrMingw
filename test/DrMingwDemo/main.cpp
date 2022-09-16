#include <QCoreApplication>
#include <QThread>

extern "C" void ExcHndlInit(const char *pCrashFileName);

class TestClass : public QThread
{
public:
    void run() override
    {
        int* a = (int*)(NULL);
        *a = 1;
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    ExcHndlInit((a.applicationDirPath() +"/" + a.applicationName() + ".RPT").toLocal8Bit().data());

    // ÷∆‘Ï±¿¿£
    TestClass instance;
    instance.start();

    return a.exec();
}

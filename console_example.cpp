#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include "cardreader.h"
#include "atrparser.h"

class ConsoleCardReader : public QObject
{
    Q_OBJECT

public:
    ConsoleCardReader(QObject *parent = nullptr)
        : QObject(parent)
        , m_cardReader(new CardReader(this))
    {
        connect(m_cardReader, &CardReader::cardInserted,
                this, &ConsoleCardReader::onCardInserted);
        connect(m_cardReader, &CardReader::cardRemoved,
                this, &ConsoleCardReader::onCardRemoved);
        connect(m_cardReader, &CardReader::readerError,
                this, &ConsoleCardReader::onReaderError);
    }

    void run()
    {
        QTextStream out(stdout);
        out << "=== ATR Parser для банковских и Mifare карт ===" << Qt::endl;
        out << Qt::endl;
        
        if (!m_cardReader->initialize()) {
            out << "ОШИБКА: Не удалось инициализировать PC/SC" << Qt::endl;
            out << "Проверьте, что служба pcscd запущена: sudo systemctl start pcscd" << Qt::endl;
            QCoreApplication::exit(1);
            return;
        }
        
        QStringList readers = m_cardReader->listReaders();
        
        if (readers.isEmpty()) {
            out << "ОШИБКА: Ридеры не найдены!" << Qt::endl;
            out << "Подключите ридер и убедитесь, что он распознан системой." << Qt::endl;
            QCoreApplication::exit(1);
            return;
        }
        
        out << "Найдено ридеров: " << readers.size() << Qt::endl;
        for (int i = 0; i < readers.size(); i++) {
            out << "  [" << i << "] " << readers[i] << Qt::endl;
        }
        out << Qt::endl;
        
        // Подключаемся к первому ридеру
        QString selectedReader = readers[0];
        out << "Подключение к: " << selectedReader << Qt::endl;
        
        if (!m_cardReader->connectToReader(selectedReader)) {
            out << "ОШИБКА: Не удалось подключиться к ридеру" << Qt::endl;
            QCoreApplication::exit(1);
            return;
        }
        
        out << "Успешно подключено!" << Qt::endl;
        out << Qt::endl;
        
        // Пробуем прочитать карту сразу
        out << "Попытка чтения карты..." << Qt::endl;
        ATRData cardInfo = m_cardReader->readCardInfo();
        
        if (!cardInfo.rawAtr.isEmpty()) {
            displayCardInfo(cardInfo);
        } else {
            out << "Карта не обнаружена в ридере" << Qt::endl;
        }
        
        // Запускаем мониторинг
        out << Qt::endl;
        out << "Запуск мониторинга карт..." << Qt::endl;
        out << "Приложите карту к ридеру. Для выхода нажмите Ctrl+C" << Qt::endl;
        out << Qt::endl;
        
        m_cardReader->startMonitoring(500);
    }

private slots:
    void onCardInserted(const ATRData &cardInfo)
    {
        QTextStream out(stdout);
        out << Qt::endl;
        out << "╔═══════════════════════════════════════════════════════════╗" << Qt::endl;
        out << "║           🔔 КАРТА ОБНАРУЖЕНА!                            ║" << Qt::endl;
        out << "╚═══════════════════════════════════════════════════════════╝" << Qt::endl;
        displayCardInfo(cardInfo);
    }
    
    void onCardRemoved()
    {
        QTextStream out(stdout);
        out << Qt::endl;
        out << "🔔 Карта извлечена" << Qt::endl;
        out << Qt::endl;
    }
    
    void onReaderError(const QString &error)
    {
        QTextStream out(stdout);
        out << "ОШИБКА: " << error << Qt::endl;
    }

private:
    void displayCardInfo(const ATRData &cardInfo)
    {
        QTextStream out(stdout);

        // Получаем красиво отформатированный вывод из парсера
        ATRParser parser;
        parser.parseATR(cardInfo.rawAtr);
        if (cardInfo.hasATS)
            parser.parseATS(cardInfo.atsRaw);
        QString formattedOutput = parser.getFormattedOutput();

        out << formattedOutput << Qt::endl;
    }
    
    CardReader *m_cardReader;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    ConsoleCardReader reader;
    QTimer::singleShot(0, &reader, &ConsoleCardReader::run);
    
    return app.exec();
}

#include "console_example.moc"

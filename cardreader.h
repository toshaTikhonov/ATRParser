#ifndef CARDREADER_H
#define CARDREADER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QTimer>

#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif

#include "atrparser.h"

class CardReader : public QObject
{
    Q_OBJECT

public:
    explicit CardReader(QObject *parent = nullptr);
    ~CardReader();
    
    // Инициализация и управление
    bool initialize();
    void cleanup();
    
    // Работа с ридерами
    QStringList listReaders();
    bool connectToReader(const QString &readerName);
    void disconnect();
    
    // Информация о подключении
    bool isConnected() const { return m_connected; }
    QString currentReader() const { return m_currentReader; }
    
    // Работа с картой
    QVector<uint8_t> getATR();
    ATRData readCardInfo();
    QVector<uint8_t> getATS(); // чтение ATS
    // Автоматическое обнаружение карт
    void startMonitoring(int intervalMs = 1000);
    void stopMonitoring();
    
signals:
    void cardInserted(const ATRData &cardInfo);
    void cardRemoved();
    void readerError(const QString &error);
    void readersListChanged(const QStringList &readers);

private slots:
    void checkCardPresence();

private:
    struct ReaderState {
        QString name;
        SCARDHANDLE handle = 0;
        DWORD protocol = 0;
        bool connected = false;
        bool cardPresent = false;
        QVector<uint8_t> lastATR;
    };
    SCARDCONTEXT m_context;
//    SCARDHANDLE m_card;
//    DWORD m_protocol;
    
    bool m_initialized;
    bool m_connected;
    QString m_currentReader;
    
    QTimer *m_monitorTimer;
//    bool m_cardPresent;
    QVector<uint8_t> m_lastATR;
    
    ATRParser m_parser;
    // Новое: состояние по всем ридерам
    QMap<QString, ReaderState> m_readers;

    // Вспомогательные методы
    QString getErrorString(LONG result) const;
    bool checkCardStatusFor(ReaderState &rs);
    QVector<uint8_t> getATRFor(const ReaderState &rs);

};

#endif // CARDREADER_H

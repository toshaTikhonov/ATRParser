#ifndef ATRPARSER_H
#define ATRPARSER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>

// Типы карт
enum class CardType {
    Unknown,
    BankCard_EMV,
    Mifare_Classic,
    Mifare_DESFire,
    Mifare_Ultralight,
    Mifare_Plus,
    ISO14443A,
    ISO14443B
};

// Структура для детального парсинга interface bytes
struct InterfaceByteDetails {
    struct TABytes {
        QVector<uint8_t> values;
        int clockRateConversion;  // Fi
        int bitRateAdjustment;    // Di
        int baudRate;

        TABytes() : clockRateConversion(372), bitRateAdjustment(1), baudRate(9600) {}
    };

    struct TBBytes {
        QVector<uint8_t> values;
        int programmingVoltage;   // VPP
        int programmingCurrent;   // IPP

        TBBytes() : programmingVoltage(0), programmingCurrent(0) {}
    };

    struct TCBytes {
        QVector<uint8_t> values;
        int guardTime;            // N (extra guard time)
        int waitingTime;          // WI (waiting time integer)

        TCBytes() : guardTime(0), waitingTime(10) {}
    };

    struct TDBytes {
        QVector<uint8_t> values;
        QVector<int> protocols;   // Список протоколов из каждого TD
    };

    TABytes ta;
    TBBytes tb;
    TCBytes tc;
    TDBytes td;
};

// Структура для хранения распарсенного ATR
struct ATRData {
    QVector<uint8_t> rawAtr;
    uint8_t ts;              // Initial character
    uint8_t t0;              // Format character
    QVector<uint8_t> interfaceBytes;
    QVector<uint8_t> historicalBytes;
    uint8_t tck;             // Check character (если есть)
    bool hasTck;

    // Протоколы
    QVector<int> supportedProtocols;

    // Детальный парсинг interface bytes
    InterfaceByteDetails interfaceDetails;

    // Информация о карте
    CardType cardType;
    QString cardName;
    QString manufacturer;

    ATRData() : ts(0), t0(0), tck(0), hasTck(false), cardType(CardType::Unknown) {}
};

class ATRParser : public QObject
{
    Q_OBJECT

public:
    explicit ATRParser(QObject *parent = nullptr);
    ~ATRParser();
    
    // Основные методы парсинга
    bool parseATR(const QVector<uint8_t> &atr);
    bool parseATR(const uint8_t *atr, size_t length);
    
    // Получение результатов
    ATRData getATRData() const { return m_atrData; }
    CardType getCardType() const { return m_atrData.cardType; }
    QString getCardName() const { return m_atrData.cardName; }
    QString getManufacturer() const { return m_atrData.manufacturer; }
    QVector<int> getSupportedProtocols() const { return m_atrData.supportedProtocols; }
    
    // Утилиты
    QString atrToString() const;
    QString getDetailedInfo();
    QString getFormattedOutput();  // Новый метод для красивого вывода
    static QString cardTypeToString(CardType type);
    
signals:
    void cardDetected(CardType type, const QString &name);
    void parsingError(const QString &error);

private:
    ATRData m_atrData;
    
    // Внутренние методы парсинга
    bool parseInterfaceBytes();
    void parseInterfaceBytesDetailed();  // Новый метод для детального парсинга
    void detectCardType();
    bool verifyChecksum();
    
    // Определение типов карт
    bool isMifareClassic() const;
    bool isMifareDESFire() const;
    bool isMifareUltralight() const;
    bool isMifarePlus() const;
    bool isEMVBankCard();
    
    // Словарь известных ATR
    void initKnownATRs();
    QMap<QString, QPair<CardType, QString>> m_knownATRs;
    
    // Определение производителя по историческим байтам
    QString detectManufacturer() const;
};

#endif // ATRPARSER_H

#include "cardreader.h"
#include <QDebug>
#include <cstring>

CardReader::CardReader(QObject *parent)
    : QObject(parent)
    , m_context(0)
    , m_card(0)
    , m_protocol(0)
    , m_initialized(false)
    , m_connected(false)
    , m_cardPresent(false)
    , m_parser(this)
{
    m_monitorTimer = new QTimer(this);
    connect(m_monitorTimer, &QTimer::timeout, this, &CardReader::checkCardPresence);
}

CardReader::~CardReader()
{
    cleanup();
}

bool CardReader::initialize()
{
    if (m_initialized) {
        return true;
    }
    
    LONG result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &m_context);
    
    if (result != SCARD_S_SUCCESS) {
        emit readerError(QString("Ошибка инициализации PC/SC: %1").arg(getErrorString(result)));
        return false;
    }
    
    m_initialized = true;
    qDebug() << "PC/SC контекст успешно инициализирован";
    return true;
}

void CardReader::cleanup()
{
    stopMonitoring();
    disconnect();
    
    if (m_initialized) {
        SCardReleaseContext(m_context);
        m_initialized = false;
        m_context = 0;
    }
}

QStringList CardReader::listReaders()
{
    QStringList readers;
    
    if (!m_initialized) {
        if (!initialize()) {
            return readers;
        }
    }
    
    DWORD readersLen = 0;
    LONG result = SCardListReaders(m_context, nullptr, nullptr, &readersLen);
    
    if (result != SCARD_S_SUCCESS) {
        emit readerError(QString("Ошибка получения списка ридеров: %1").arg(getErrorString(result)));
        return readers;
    }
    
    if (readersLen == 0) {
        emit readerError("Ридеры не найдены");
        return readers;
    }
    
    QVector<char> readersBuffer(readersLen);
    result = SCardListReaders(m_context, nullptr, readersBuffer.data(), &readersLen);
    
    if (result != SCARD_S_SUCCESS) {
        emit readerError(QString("Ошибка чтения списка ридеров: %1").arg(getErrorString(result)));
        return readers;
    }
    
    // Парсинг multi-string буфера
    char *ptr = readersBuffer.data();
    while (*ptr != '\0') {
        QString readerName = QString::fromLocal8Bit(ptr);
        readers.append(readerName);
        ptr += strlen(ptr) + 1;
    }
    
    emit readersListChanged(readers);
    return readers;
}

bool CardReader::connectToReader(const QString &readerName)
{
    if (!m_initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    if (m_connected) {
        disconnect();
    }
    
    QByteArray readerNameBytes = readerName.toLocal8Bit();
    LONG result = SCardConnect(
        m_context,
        readerNameBytes.constData(),
        SCARD_SHARE_SHARED,
        SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
        &m_card,
        &m_protocol
    );
    
    if (result != SCARD_S_SUCCESS) {
        if (result != SCARD_E_NO_SMARTCARD && result != SCARD_W_REMOVED_CARD) {

        emit readerError(QString("Ошибка подключения к ридеру '%1': %2")
            .arg(readerName)
            .arg(getErrorString(result)));
        return false;
        }
    }
    
    m_connected = true;
    m_currentReader = readerName;
    
    qDebug() << "Успешно подключено к ридеру:" << readerName;
    qDebug() << "Протокол:" << (m_protocol == SCARD_PROTOCOL_T0 ? "T=0" : "T=1");
    
    return true;
}

void CardReader::disconnect()
{
    if (m_connected) {
        SCardDisconnect(m_card, SCARD_LEAVE_CARD);
        m_connected = false;
        m_card = 0;
        m_currentReader.clear();
        qDebug() << "Отключено от ридера";
    }
}

QVector<uint8_t> CardReader::getATR()
{
    QVector<uint8_t> atr;
    
    if (!m_connected) {
        emit readerError("Нет подключения к ридеру");
        return atr;
    }
    
    BYTE atrBuffer[MAX_ATR_SIZE];
    DWORD atrLen = sizeof(atrBuffer);
    DWORD state, protocol;
    BYTE readerName[256];
    DWORD readerLen = sizeof(readerName);
    
    LONG result = SCardStatus(
        m_card,
        reinterpret_cast<LPSTR>(readerName),
        &readerLen,
        &state,
        &protocol,
        atrBuffer,
        &atrLen
    );
    
    if (result != SCARD_S_SUCCESS) {
        // Не считаем отсутствующую карту ошибкой
        if (result == SCARD_E_NO_SMARTCARD || result == SCARD_W_REMOVED_CARD) {
            return atr;
        }
        emit readerError(QString("Ошибка чтения ATR: %1").arg(getErrorString(result)));
        return atr;
    }
    
    for (DWORD i = 0; i < atrLen; i++) {
        atr.append(atrBuffer[i]);
    }
    
    return atr;
}
QVector<uint8_t> CardReader::getATS()
{
    QVector<uint8_t> ats;

    if (!m_connected) {
        // отсутствие карты не считаем ошибкой, просто пусто
        return ats;
    }

    // GET DATA (ATS) команда в PC/SC:
    // Команда: FF CA 01 00 00 — НЕ правильная для ATS, это UID.
    // Для ATS используем: FF CA 36 00 00? — тоже неверно.
    // Корректно: команда ISO7816-4 GET DATA с P1P2=0x9F 0x7F, но через vendor escape не всегда доступна.
    // На практике для 14443-4 (contactless) часто доступна команда:
    //   FF CA 01 00 00 — UID; ATS может быть по: FF CA 36 00 00 (NXP) или команда 0xCA GET DATA P1=0x01(P2=0x00) не стандарт.
    // Универсальный способ через PC/SC: SCardTransmit с APDU: 00 CA 01 00 00 — GET DATA (ATS) по P1=0x01?
    // В большинстве ридеров ожидаемый тег ATS — 0x36 (proprietary). Надежнее запрос по GET DATA tag 0x36:
    //   APDU: FF CA 36 00 00
    // Реализации различаются, поэтому попробуем несколько известных вариантов по очереди.

    const QByteArray apdus[] = {
  //      QByteArray::fromHex("00CA017F00"), // GET DATA P1=0x01,P2=0x7F (некоторые стекы)
  //      QByteArray::fromHex("00CA9F7F00"), // GET DATA P1P2=0x9F7F (ATS tag)
  //      QByteArray::fromHex("FFCA360000"),  // Vendor GET DATA ATS (часто для ACR/NXP)
        QByteArray::fromHex("FFCA010000")
    };

    SCARD_IO_REQUEST sendPci;
    sendPci.dwProtocol = m_protocol;
    sendPci.cbPciLength = sizeof(SCARD_IO_REQUEST);
    BYTE recvBuf[258];
    DWORD recvLen;

    for (const QByteArray &apdu : apdus) {
        recvLen = sizeof(recvBuf);
        LONG r = SCardTransmit(m_card, &sendPci,
                               reinterpret_cast<const BYTE*>(apdu.constData()),
                               static_cast<DWORD>(apdu.size()),
                               nullptr, recvBuf, &recvLen);
        if (r != SCARD_S_SUCCESS) {
            // другие ошибки игнорируем и пробуем следующий вариант
            continue;
        }
        if (recvLen < 2) continue;

        // Последние 2 байта — SW1 SW2
        const BYTE sw1 = recvBuf[recvLen - 2];
        const BYTE sw2 = recvBuf[recvLen - 1];
        if (!(sw1 == 0x90 && sw2 == 0x00)) {
            continue;
        }

        // Данные до SW1SW2 — это ATS
        const DWORD dataLen = recvLen - 2;
        if (dataLen == 0) continue;

        ats.reserve(static_cast<int>(dataLen));
        for (DWORD i = 0; i < dataLen; ++i) {
            ats.push_back(recvBuf[i]);
        }
        break;
    }

    return ats;
}

ATRData CardReader::readCardInfo()
{
    ATRData emptyData;
    
    QVector<uint8_t> atr = getATR();
    if (atr.isEmpty()) {
        return emptyData;
    }
    
    if (!m_parser.parseATR(atr)) {
        emit readerError("Ошибка парсинга ATR");
        return emptyData;
    }
    // Попытка прочитать ATS и распарсить (если карта 14443-4)
    QVector<uint8_t> ats = getATS();
    if (!ats.isEmpty()) {
        m_parser.parseATS(ats);
    }
    return m_parser.getATRData();
}

void CardReader::startMonitoring(int intervalMs)
{
    if (!m_connected) {
        emit readerError("Нельзя начать мониторинг без подключения к ридеру");
        return;
    }
    
    m_cardPresent = checkCardStatus();
    m_monitorTimer->start(intervalMs);
    qDebug() << "Мониторинг карт запущен с интервалом" << intervalMs << "мс";
}

void CardReader::stopMonitoring()
{
    m_monitorTimer->stop();
    qDebug() << "Мониторинг карт остановлен";
}

void CardReader::checkCardPresence()
{
    if (!m_connected) {
        return;
    }
    
    bool cardNowPresent = checkCardStatus();
    
    // Карта вставлена
    if (cardNowPresent && !m_cardPresent) {
        m_cardPresent = true;
        
        ATRData cardInfo = readCardInfo();
        if (cardInfo.cardType != CardType::Unknown) {
            qDebug() << "Карта обнаружена:" << cardInfo.cardName;
            emit cardInserted(cardInfo);
        }
    }
    // Карта извлечена
    else if (!cardNowPresent && m_cardPresent) {
        m_cardPresent = false;
        qDebug() << "Карта извлечена";
        emit cardRemoved();
    }
}

bool CardReader::checkCardStatus()
{
    if (!m_connected) {
        return false;
    }
    
    BYTE readerName[256];
    DWORD readerLen = sizeof(readerName);
    DWORD state, protocol;
    BYTE atr[MAX_ATR_SIZE];
    DWORD atrLen = sizeof(atr);
    
    LONG result = SCardStatus(
        m_card,
        reinterpret_cast<LPSTR>(readerName),
        &readerLen,
        &state,
        &protocol,
        atr,
        &atrLen
    );
    
    if (result != SCARD_S_SUCCESS) {
        // Возможно карта была извлечена, пробуем переподключиться
        if (result == SCARD_W_REMOVED_CARD || result == SCARD_E_NO_SMARTCARD) {
            return false;
        }
        
        // Пробуем переподключиться
        QString reader = m_currentReader;
        disconnect();
        connectToReader(reader);
        
        return false;
    }
    
    return (state & SCARD_PRESENT) != 0;
}

QString CardReader::getErrorString(LONG result) const
{
    const DWORD code = static_cast<DWORD>(result);
    switch (code) {
        case SCARD_S_SUCCESS:
            return "Успешно";
        case SCARD_E_INVALID_HANDLE:
            return "Неверный дескриптор";
        case SCARD_E_INVALID_PARAMETER:
            return "Неверный параметр";
        case SCARD_E_NO_MEMORY:
            return "Недостаточно памяти";
        case SCARD_E_NO_SERVICE:
            return "Служба PC/SC не запущена";
        case SCARD_E_NO_SMARTCARD:
            return "Карта не обнаружена";
        case SCARD_E_NOT_READY:
            return "Ридер не готов";
        case SCARD_E_READER_UNAVAILABLE:
            return "Ридер недоступен";
        case SCARD_E_SHARING_VIOLATION:
            return "Ридер используется другим приложением";
        case SCARD_E_TIMEOUT:
            return "Превышено время ожидания";
        case SCARD_W_REMOVED_CARD:
            return "Карта извлечена";
        case SCARD_W_RESET_CARD:
            return "Карта была сброшена";
        case SCARD_W_UNPOWERED_CARD:
            return "Карта не запитана";
        case SCARD_W_UNRESPONSIVE_CARD:
            return "Карта не отвечает";
        default:
            return QString("Ошибка 0x%1").arg(QString::number(code, 16).rightJustified(8, QChar('0')));
    }
}

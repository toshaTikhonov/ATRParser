#include "cardreader.h"
#include <QDebug>
#include <cstring>

CardReader::CardReader(QObject *parent)
    : QObject(parent)
    , m_context(0)
    , m_initialized(false)
    , m_connected(false)
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
    
    // отключаем все ридеры
    for (auto &rs : m_readers) {
        if (rs.connected) {
            SCardDisconnect(rs.handle, SCARD_LEAVE_CARD);
            rs.connected = false;
            rs.handle = 0;
        }
    }
    m_readers.clear();
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
    // Заполняем карту состояний (без подключения)
    QMap<QString, ReaderState> newMap;
    for (const QString &r : readers) {
        ReaderState rs;
        rs.name = r;
        newMap.insert(r, rs);
    }
    m_readers = newMap;
    emit readersListChanged(readers);
    return readers;
}

bool CardReader::connectToReader(const QString &readerName)
{
    if (!m_initialized) {
        if (!initialize()) return false;
    }

    // Подключаем только один выбранный ридер (совместимость со старым API),
    // но также сохраняем состояние в m_readers, чтобы мониторить несколько при необходимости.
    if (!m_readers.contains(readerName)) {
        listReaders(); // обновим список
    }
    ReaderState &rs = m_readers[readerName];

    if (rs.connected) {
        SCardDisconnect(rs.handle, SCARD_LEAVE_CARD);
        rs.connected = false;
        rs.handle = 0;
    }

    QByteArray readerNameBytes = readerName.toLocal8Bit();
    SCARDHANDLE handle = 0;
    DWORD protocol = 0;
    LONG result = SCardConnect(
        m_context,
        readerNameBytes.constData(),
        SCARD_SHARE_SHARED,
        SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
        &handle,
        &protocol
    );

    if (result != SCARD_S_SUCCESS) {
        emit readerError(QString("Ошибка подключения к ридеру '%1': %2")
            .arg(readerName)
            .arg(getErrorString(result)));
        return false;
    }

    rs.handle = handle;
    rs.protocol = protocol;
    rs.connected = true;
    rs.cardPresent = false;
    rs.lastATR.clear();

    m_connected = true;
    m_currentReader = readerName;

    qDebug() << "Успешно подключено к ридеру:" << readerName;
    qDebug() << "Протокол:" << (protocol == SCARD_PROTOCOL_T0 ? "T=0" : "T=1");

    return true;
}

void CardReader::disconnect()
{
    // отключаем только активный ридер из m_currentReader
    if (!m_currentReader.isEmpty() && m_readers.contains(m_currentReader)) {
        ReaderState &rs = m_readers[m_currentReader];
        if (rs.connected) {
            SCardDisconnect(rs.handle, SCARD_LEAVE_CARD);
            rs.connected = false;
            rs.handle = 0;
            qDebug() << "Отключено от ридера:" << rs.name;
        }
    }
    m_connected = false;
    m_currentReader.clear();
}
QVector<uint8_t> CardReader::getATRFor(const ReaderState &rs)
{
    QVector<uint8_t> atr;
    if (!rs.connected) return atr;

    BYTE atrBuffer[MAX_ATR_SIZE];
    DWORD atrLen = sizeof(atrBuffer);
    DWORD state, protocol;
    BYTE readerName[256];
    DWORD readerLen = sizeof(readerName);

    LONG result = SCardStatus(
        rs.handle,
        reinterpret_cast<LPSTR>(readerName),
        &readerLen,
        &state,
        &protocol,
        atrBuffer,
        &atrLen
    );

    if (result != SCARD_S_SUCCESS) {
        return atr;
    }

    for (DWORD i = 0; i < atrLen; i++) {
        atr.append(atrBuffer[i]);
    }
    return atr;
}

QVector<uint8_t> CardReader::getATR()
{
    // для совместимости: возвращаем ATR активного ридера
    if (m_currentReader.isEmpty() || !m_readers.contains(m_currentReader)) return {};
    return getATRFor(m_readers[m_currentReader]);
}
QVector<uint8_t> CardReader::getATS()
{
    QVector<uint8_t> ats;

    // Используем активный ридер
    if (m_currentReader.isEmpty() || !m_readers.contains(m_currentReader))
        return ats;
    const ReaderState &rs = m_readers[m_currentReader];
    if (!rs.connected)
        return ats;

    // SCardTransmit требует корректный PCI по протоколу
    const SCARD_IO_REQUEST* pci =
        (rs.protocol == SCARD_PROTOCOL_T0) ? SCARD_PCI_T0 :
        (rs.protocol == SCARD_PROTOCOL_T1) ? SCARD_PCI_T1 :
        nullptr;

    if (!pci) return ats;

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
        QByteArray::fromHex("00CA017F00"), // GET DATA P1=0x01,P2=0x7F (некоторые стекы)
        QByteArray::fromHex("00CA9F7F00"), // GET DATA P1P2=0x9F7F (ATS tag)
        QByteArray::fromHex("FFCA360000"),  // Vendor GET DATA ATS (часто для ACR/NXP)
        QByteArray::fromHex("FFCA010000")
    };

    BYTE recvBuf[512];
    for (const QByteArray &apdu : apdus) {
        DWORD recvLen = sizeof(recvBuf);
        LONG r = SCardTransmit(rs.handle,
                               pci,
                               reinterpret_cast<const BYTE*>(apdu.constData()),
                               static_cast<DWORD>(apdu.size()),
                               nullptr,
                               recvBuf,
                               &recvLen);
        if (r != SCARD_S_SUCCESS || recvLen < 2)
            continue;

        BYTE sw1 = recvBuf[recvLen - 2];
        BYTE sw2 = recvBuf[recvLen - 1];
        if (sw1 != 0x90 || sw2 != 0x00)
            continue;

        DWORD dataLen = recvLen - 2;
        if (dataLen == 0)
            continue;

        ats.reserve(static_cast<int>(dataLen));
        for (DWORD i = 0; i < dataLen; ++i)
            ats.push_back(recvBuf[i]);

        break; // успешно получили ATS
    }

    return ats;
}

ATRData CardReader::readCardInfo()
{
    ATRData emptyData;
    if (m_currentReader.isEmpty() || !m_readers.contains(m_currentReader)) return emptyData;

    QVector<uint8_t> atr = getATRFor(m_readers[m_currentReader]);
    if (atr.isEmpty()) return emptyData;

    if (!m_parser.parseATR(atr)) {
        emit readerError("Ошибка парсинга ATR");
        return emptyData;
    }

    QVector<uint8_t> ats = getATS();
    if (!ats.isEmpty()) {
        m_parser.parseATS(ats);
    }
    return m_parser.getATRData();
}

void CardReader::startMonitoring(int intervalMs)
{
    if (!m_initialized) {
        emit readerError("Нельзя начать мониторинг без инициализации");
        return;
    }

    // Если в m_readers нет подключённых — пробуем подключить все доступные, не падая на ошибках
    if (m_readers.isEmpty()) {
        listReaders();
    }
    for (auto it = m_readers.begin(); it != m_readers.end(); ++it) {
        ReaderState &rs = it.value();
        if (!rs.connected) {
            // тихо пробуем подключиться; ошибки не критичны для общего мониторинга
            connectToReader(rs.name);
        }
        // Инициализируем начальное состояние
        rs.cardPresent = checkCardStatusFor(rs);
        rs.lastATR = rs.cardPresent ? getATRFor(rs) : QVector<uint8_t>{};
    }

    m_monitorTimer->start(intervalMs);
    qDebug() << "Мониторинг карт запущен для" << m_readers.size() << "ридеров, интервал" << intervalMs << "мс";
}

void CardReader::stopMonitoring()
{
    m_monitorTimer->stop();
    qDebug() << "Мониторинг карт остановлен";
}

bool CardReader::checkCardStatusFor(ReaderState &rs)
{
    if (!rs.connected) return false;

    BYTE readerName[256];
    DWORD readerLen = sizeof(readerName);
    DWORD state, protocol;
    BYTE atr[MAX_ATR_SIZE];
    DWORD atrLen = sizeof(atr);

    LONG result = SCardStatus(
        rs.handle,
        reinterpret_cast<LPSTR>(readerName),
        &readerLen,
        &state,
        &protocol,
        atr,
        &atrLen
    );

    if (result != SCARD_S_SUCCESS) {
        // Нет карты — не ошибка
        if (result == SCARD_W_REMOVED_CARD || result == SCARD_E_NO_SMARTCARD) {
            return false;
        }
        // Пробуем переподключиться молча
        QByteArray rn = rs.name.toLocal8Bit();
        SCardDisconnect(rs.handle, SCARD_LEAVE_CARD);
        rs.connected = false;
        rs.handle = 0;
        DWORD proto = 0;
        SCARDHANDLE h = 0;
        if (SCardConnect(m_context, rn.constData(), SCARD_SHARE_SHARED,
                         SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
                         &h, &proto) == SCARD_S_SUCCESS) {
            rs.handle = h;
            rs.protocol = proto;
            rs.connected = true;
        }
        return false;
    }

    return (state & SCARD_PRESENT) != 0;
}

void CardReader::checkCardPresence()
{
    // Обходим все ридеры и детектим изменения состояния
    for (auto it = m_readers.begin(); it != m_readers.end(); ++it) {
        ReaderState &rs = it.value();

        if (!rs.connected) continue;

        bool nowPresent = checkCardStatusFor(rs);

        // Вставка
        if (nowPresent && !rs.cardPresent) {
            rs.cardPresent = true;
            rs.lastATR = getATRFor(rs);

            // Локальный парсер для формирования ATRData
            ATRParser parser;
            if (!rs.lastATR.isEmpty() && parser.parseATR(rs.lastATR)) {
                // Попытка ATS
                QVector<uint8_t> ats = getATS(); // при необходимости адаптируйте под конкретный ридер
                if (!ats.isEmpty()) parser.parseATS(ats);
                emit cardInserted(parser.getATRData());
            } else {
                emit cardInserted(ATRData{});
            }
        }
        // Извлечение
        else if (!nowPresent && rs.cardPresent) {
            rs.cardPresent = false;
            rs.lastATR.clear();
            emit cardRemoved();
        }
    }
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

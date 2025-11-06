#include "atrparser.h"
#include <QDebug>

static QString bytesToHex(const QVector<uint8_t>& v)
{
    QString s;
    s.reserve(v.size() * 3);
    for (int i = 0; i < v.size(); ++i) {
        s += QString::asprintf("%02X", v[i]);
        if (i + 1 < v.size()) s += ' ';
    }
    return s;
}

ATRParser::ATRParser(QObject *parent)
    : QObject(parent)
{
    initKnownATRs();
}

ATRParser::~ATRParser()
{
}

bool ATRParser::parseATR(const QVector<uint8_t> &atr)
{
    if (atr.isEmpty() || atr.size() < 2) {
        emit parsingError("ATR слишком короткий");
        return false;
    }
    
    m_atrData = ATRData();
    m_atrData.rawAtr = atr;
    
    // Парсинг TS (Initial character)
    m_atrData.ts = atr[0];
    if (m_atrData.ts != 0x3B && m_atrData.ts != 0x3F) {
        emit parsingError(QString("Неверный TS байт: 0x%1").arg(m_atrData.ts, 2, 16, QChar('0')));
        return false;
    }
    
    // Парсинг T0 (Format character)
    m_atrData.t0 = atr[1];
    int historicalBytesCount = m_atrData.t0 & 0x0F;
    
    // Парсинг interface bytes
    if (!parseInterfaceBytes()) {
        return false;
    }

    // Детальный парсинг interface bytes
    parseInterfaceBytesDetailed();

    // Извлечение исторических байтов
    int histStartIdx = 2 + m_atrData.interfaceBytes.size();
    if (histStartIdx + historicalBytesCount <= atr.size()) {
        for (int i = 0; i < historicalBytesCount; i++) {
            m_atrData.historicalBytes.append(atr[histStartIdx + i]);
        }
    }
    
    // Проверка контрольной суммы (TCK)
    int tckIdx = histStartIdx + historicalBytesCount;
    if (m_atrData.supportedProtocols.size() > 0 && 
        m_atrData.supportedProtocols[0] != 0) {
        m_atrData.hasTck = true;
        if (tckIdx < atr.size()) {
            m_atrData.tck = atr[tckIdx];
            if (!verifyChecksum()) {
                qWarning() << "Контрольная сумма ATR не совпадает!";
            }
        }
    }
    
    // Определение типа карты
    detectCardType();
    
    emit cardDetected(m_atrData.cardType, m_atrData.cardName);
    
    return true;
}

bool ATRParser::parseATR(const uint8_t *atr, size_t length)
{
    QVector<uint8_t> atrVec;
    for (size_t i = 0; i < length; i++) {
        atrVec.append(atr[i]);
    }
    return parseATR(atrVec);
}

bool ATRParser::parseInterfaceBytes()
{
    const QVector<uint8_t> &atr = m_atrData.rawAtr;
    int idx = 2;
    uint8_t td = m_atrData.t0;
    
    while (idx < atr.size()) {
        int interfaceByteCount = 0;
        
        // TA
        if (td & 0x10) {
            if (idx >= atr.size()) return false;
            m_atrData.interfaceBytes.append(atr[idx++]);
            interfaceByteCount++;
        }
        
        // TB
        if (td & 0x20) {
            if (idx >= atr.size()) return false;
            m_atrData.interfaceBytes.append(atr[idx++]);
            interfaceByteCount++;
        }
        
        // TC
        if (td & 0x40) {
            if (idx >= atr.size()) return false;
            m_atrData.interfaceBytes.append(atr[idx++]);
            interfaceByteCount++;
        }
        
        // TD
        if (td & 0x80) {
            if (idx >= atr.size()) return false;
            td = atr[idx];
            m_atrData.interfaceBytes.append(atr[idx++]);
            
            // Определение поддерживаемого протокола
            int protocol = td & 0x0F;
            if (!m_atrData.supportedProtocols.contains(protocol)) {
                m_atrData.supportedProtocols.append(protocol);
            }
        } else {
            break; // Нет больше TD байтов
        }
    }
    
    return true;
}

void ATRParser::parseInterfaceBytesDetailed()
{
    const QVector<uint8_t> &atr = m_atrData.rawAtr;
    int idx = 2;
    uint8_t td = m_atrData.t0;
    int interfaceGroup = 1;

    // Таблицы для декодирования значений TA1
    static const int Fi_table[] = {372, 372, 558, 744, 1116, 1488, 1860, -1, -1, 512, 768, 1024, 1536, 2048, -1, -1};
    static const int Di_table[] = {-1, 1, 2, 4, 8, 16, 32, 64, 12, 20, -1, -1, -1, -1, -1, -1};

    while (idx < atr.size()) {
        // TA
        if (td & 0x10) {
            if (idx >= atr.size()) break;
            uint8_t ta = atr[idx++];
            m_atrData.interfaceDetails.ta.values.append(ta);

            // Особая обработка для TA1
            if (interfaceGroup == 1) {
                int fi_index = (ta >> 4) & 0x0F;
                int di_index = ta & 0x0F;

                if (fi_index < 16 && Fi_table[fi_index] > 0) {
                    m_atrData.interfaceDetails.ta.clockRateConversion = Fi_table[fi_index];
                }
                if (di_index < 16 && Di_table[di_index] > 0) {
                    m_atrData.interfaceDetails.ta.bitRateAdjustment = Di_table[di_index];
                }

                // Расчет скорости передачи данных
                if (m_atrData.interfaceDetails.ta.clockRateConversion > 0 &&
                    m_atrData.interfaceDetails.ta.bitRateAdjustment > 0) {
                    m_atrData.interfaceDetails.ta.baudRate =
                        (3750000 * m_atrData.interfaceDetails.ta.bitRateAdjustment) /
                        m_atrData.interfaceDetails.ta.clockRateConversion;
                }
            }
        }

        // TB
        if (td & 0x20) {
            if (idx >= atr.size()) break;
            uint8_t tb = atr[idx++];
            m_atrData.interfaceDetails.tb.values.append(tb);

            // TB1: Programming voltage and current
            if (interfaceGroup == 1) {
                m_atrData.interfaceDetails.tb.programmingVoltage = (tb >> 5) & 0x07;
                m_atrData.interfaceDetails.tb.programmingCurrent = tb & 0x1F;
            }
        }

        // TC
        if (td & 0x40) {
            if (idx >= atr.size()) break;
            uint8_t tc = atr[idx++];
            m_atrData.interfaceDetails.tc.values.append(tc);

            // TC1: Extra guard time
            if (interfaceGroup == 1) {
                m_atrData.interfaceDetails.tc.guardTime = tc;
            }
            // TC2: Waiting time integer (для протокола T=0)
            else if (interfaceGroup == 2) {
                m_atrData.interfaceDetails.tc.waitingTime = tc;
            }
        }

        // TD
        if (td & 0x80) {
            if (idx >= atr.size()) break;
            td = atr[idx];
            m_atrData.interfaceDetails.td.values.append(atr[idx++]);

            int protocol = td & 0x0F;
            m_atrData.interfaceDetails.td.protocols.append(protocol);

            interfaceGroup++;
        } else {
            break;
        }
    }
}

void ATRParser::detectCardType()
{
    // Сначала проверяем известные ATR
    QString atrHex = atrToString();
    if (m_knownATRs.contains(atrHex)) {
        auto cardInfo = m_knownATRs[atrHex];
        m_atrData.cardType = cardInfo.first;
        m_atrData.cardName = cardInfo.second;
        m_atrData.manufacturer = detectManufacturer();
        return;
    }
    
    // Проверка на Mifare карты
    if (isMifareClassic()) {
        m_atrData.cardType = CardType::Mifare_Classic;
        m_atrData.cardName = "Mifare Classic";
    } else if (isMifareDESFire()) {
        m_atrData.cardType = CardType::Mifare_DESFire;
        m_atrData.cardName = "Mifare DESFire";
    } else if (isMifareUltralight()) {
        m_atrData.cardType = CardType::Mifare_Ultralight;
        m_atrData.cardName = "Mifare Ultralight";
    } else if (isMifarePlus()) {
        m_atrData.cardType = CardType::Mifare_Plus;
        m_atrData.cardName = "Mifare Plus";
    }
    // Проверка на банковские EMV карты
    else if (isEMVBankCard()) {
        m_atrData.cardType = CardType::BankCard_EMV;
        m_atrData.cardName = "Банковская карта (EMV)";
    }
    // Общие типы ISO
    else if (m_atrData.ts == 0x3B) {
        m_atrData.cardType = CardType::ISO14443A;
        m_atrData.cardName = "ISO 14443-A карта";
    } else if (m_atrData.ts == 0x3F) {
        m_atrData.cardType = CardType::ISO14443B;
        m_atrData.cardName = "ISO 14443-B карта";
    } else {
        m_atrData.cardType = CardType::Unknown;
        m_atrData.cardName = "Неизвестная карта";
    }
    
    m_atrData.manufacturer = detectManufacturer();
}

bool ATRParser::isMifareClassic() const
{
    // Mifare Classic обычно имеет ATR начинающийся с 3B 8F 80 01 80...
    // или исторические байты содержат специфичные для Mifare данные
    if (m_atrData.rawAtr.size() >= 4) {
        if (m_atrData.rawAtr[0] == 0x3B && 
            m_atrData.rawAtr[1] == 0x8F &&
            m_atrData.rawAtr[2] == 0x80) {
            return true;
        }
    }
    
    // Проверка исторических байтов
    if (m_atrData.historicalBytes.size() >= 7) {
        // Mifare Classic часто содержит 0x03 в исторических байтах
        for (int i = 0; i < m_atrData.historicalBytes.size() - 1; i++) {
            if (m_atrData.historicalBytes[i] == 0x03 &&
                m_atrData.historicalBytes[i+1] == 0x00) {
                return true;
            }
        }
    }
    
    return false;
}

bool ATRParser::isMifareDESFire() const
{
    // DESFire имеет характерные ATR
    if (m_atrData.rawAtr.size() >= 3) {
        if (m_atrData.rawAtr[0] == 0x3B && 
            m_atrData.rawAtr[1] == 0x81 &&
            m_atrData.rawAtr[2] == 0x80) {
            return true;
        }
        if (m_atrData.rawAtr[0] == 0x3B && 
            m_atrData.rawAtr[1] == 0x86 &&
            m_atrData.rawAtr[2] == 0x80) {
            return true;
        }
    }
    
    // Проверка по историческим байтам (DESFire обычно содержит 0x75 0x77 0x81)
    if (m_atrData.historicalBytes.size() >= 3) {
        for (int i = 0; i <= m_atrData.historicalBytes.size() - 3; i++) {
            if (m_atrData.historicalBytes[i] == 0x75 &&
                m_atrData.historicalBytes[i+1] == 0x77 &&
                m_atrData.historicalBytes[i+2] == 0x81) {
                return true;
            }
        }
    }
    
    return false;
}

bool ATRParser::isMifareUltralight() const
{
    // Ultralight обычно 3B 8F 80 01 80 4F 0C A0 00 00 03 06 03...
    if (m_atrData.rawAtr.size() >= 10) {
        if (m_atrData.rawAtr[0] == 0x3B && 
            m_atrData.rawAtr[1] == 0x8F &&
            m_atrData.rawAtr[6] == 0xA0 &&
            m_atrData.rawAtr[10] == 0x03) {
            return true;
        }
    }
    
    return false;
}

bool ATRParser::isMifarePlus() const
{
    // Mifare Plus имеет специфичный ATR
    if (m_atrData.historicalBytes.size() >= 4) {
        // Проверка на наличие маркера Mifare Plus
        for (int i = 0; i <= m_atrData.historicalBytes.size() - 4; i++) {
            if (m_atrData.historicalBytes[i] == 0x00 &&
                m_atrData.historicalBytes[i+1] == 0x01 &&
                m_atrData.historicalBytes[i+2] == 0x00) {
                return true;
            }
        }
    }
    
    return false;
}

bool ATRParser::isEMVBankCard()
{
    // EMV карты обычно поддерживают T=1 протокол
    if (m_atrData.supportedProtocols.contains(1)) {
        // Проверка наличия типичных для EMV исторических байтов
        if (m_atrData.historicalBytes.size() >= 4) {
            // EMV часто содержит category indicator и RID
            // Проверяем на наличие известных RID (Registered Application Provider Identifier)
            for (int i = 0; i <= m_atrData.historicalBytes.size() - 5; i++) {
                // Visa: A0 00 00 00 03
                if (m_atrData.historicalBytes[i] == 0xA0 &&
                    m_atrData.historicalBytes[i+1] == 0x00 &&
                    m_atrData.historicalBytes[i+2] == 0x00 &&
                    m_atrData.historicalBytes[i+3] == 0x00 &&
                    m_atrData.historicalBytes[i+4] == 0x03) {
                    m_atrData.manufacturer =   "Visa";
                    return true;
                }
                // Mastercard: A0 00 00 00 04
                if (m_atrData.historicalBytes[i] == 0xA0 &&
                    m_atrData.historicalBytes[i+1] == 0x00 &&
                    m_atrData.historicalBytes[i+2] == 0x00 &&
                    m_atrData.historicalBytes[i+3] == 0x00 &&
                    m_atrData.historicalBytes[i+4] == 0x04) {
                    m_atrData.manufacturer = "Mastercard";
                    return true;
                }
                // American Express: A0 00 00 00 25
                if (m_atrData.historicalBytes[i] == 0xA0 &&
                    m_atrData.historicalBytes[i+1] == 0x00 &&
                    m_atrData.historicalBytes[i+2] == 0x00 &&
                    m_atrData.historicalBytes[i+3] == 0x00 &&
                    m_atrData.historicalBytes[i+4] == 0x25) {
                    m_atrData.manufacturer = "American Express";
                    return true;
                }
            }
        }
        
        // Если есть T=1 и длина ATR > 12, вероятно EMV
        if (m_atrData.rawAtr.size() > 12) {
            return true;
        }
    }
    
    return false;
}

bool ATRParser::verifyChecksum()
{
    if (!m_atrData.hasTck) {
        return true; // TCK не требуется
    }
    
    uint8_t checksum = 0;
    for (int i = 1; i < m_atrData.rawAtr.size() - 1; i++) {
        checksum ^= m_atrData.rawAtr[i];
    }
    
    return checksum == m_atrData.tck;
}

QString ATRParser::detectManufacturer() const
{
    if (!m_atrData.manufacturer.isEmpty()) {
        return m_atrData.manufacturer;
    }
    
    // Определение по историческим байтам
    if (m_atrData.historicalBytes.size() >= 2) {
        uint8_t category = m_atrData.historicalBytes[0];
        
        // Стандартные category indicators
        if (category == 0x00) {
            return "Неизвестный производитель";
        } else if (category == 0x10) {
            return "Philips/NXP";
        } else if (category == 0x80) {
            return "Generic smartcard";
        }
    }
    
    return "Не определен";
}

QString ATRParser::atrToString() const
{
    QString result;
    for (uint8_t byte : m_atrData.rawAtr) {
        if (!result.isEmpty()) result += " ";
        result += QString("%1").arg(byte, 2, 16, QChar('0')).toUpper();
    }
    return result;
}

QString ATRParser::getDetailedInfo()
{
    QString info;
    info += "=== Информация о карте ===\n";
    info += QString("ATR: %1\n").arg(atrToString());
    info += QString("Тип карты: %1\n").arg(m_atrData.cardName);
    info += QString("Категория: %1\n").arg(cardTypeToString(m_atrData.cardType));
    info += QString("Производитель: %1\n\n").arg(m_atrData.manufacturer);
    
    info += "=== Технические детали ===\n";
    info += QString("TS: 0x%1 (%2)\n")
        .arg(m_atrData.ts, 2, 16, QChar('0'))
        .arg(m_atrData.ts == 0x3B ? "Прямая конвенция" : "Обратная конвенция");
    info += QString("T0: 0x%1\n").arg(m_atrData.t0, 2, 16, QChar('0'));
    info += QString("Исторические байты (%1): ").arg(m_atrData.historicalBytes.size());
    for (uint8_t byte : m_atrData.historicalBytes) {
        info += QString("%1 ").arg(byte, 2, 16, QChar('0')).toUpper();
    }
    info += "\n";
    
    if (!m_atrData.supportedProtocols.isEmpty()) {
        info += "Поддерживаемые протоколы: ";
        for (int proto : m_atrData.supportedProtocols) {
            info += QString("T=%1 ").arg(proto);
        }
        info += "\n";
    }
    
    if (m_atrData.hasTck) {
        info += QString("TCK: 0x%1 (контрольная сумма %2)\n")
            .arg(m_atrData.tck, 2, 16, QChar('0'))
            .arg(verifyChecksum() ? "OK" : "ОШИБКА!");
    }
    // Цвета ANSI (работают в консоли; в GUI игнорируются)
    auto C = [](const char* code){ return QString::fromLatin1(code); };
    const QString RESET = C("\x1b[0m");
    const QString BOLD  = C("\x1b[1m");
    const QString CYAN  = C("\x1b[36m");
    const QString GREEN = C("\x1b[32m");
    const QString YELL  = C("\x1b[33m");
    const QString MAG   = C("\x1b[35m");
    const QString BLUE  = C("\x1b[34m");
    const QString GRAY  = C("\x1b[90m");
    const QString RED   = C("\x1b[31m");

    // ATS вывод (если есть)
    if (m_atrData.hasATS) {
        info += "\n" + BOLD + CYAN + "ATS (ISO/IEC 14443-4)" + RESET + "\n";
        info += QString("%1ATS:%2 %3\n")
            .arg(BLUE, RESET, bytesToHex(m_atrData.atsRaw));
        if (m_atrData.ats_fscPresent) {
            info += QString("%1FSC:%2 %3 байт\n")
                .arg(GREEN, RESET)
                .arg(m_atrData.ats_fsc);
        }
        if (m_atrData.ats_fwi >= 0) {
            info += QString("%1FWI:%2 %3  %1(таймаут≈)%2 302µs * 2^%3\n")
                .arg(GRAY, RESET).arg(m_atrData.ats_fwi);
        }
        if (m_atrData.ats_sfgi >= 0) {
            info += QString("%1SFGI:%2 %3  %1(guard)≈)%2 302µs * 2^%3\n")
                .arg(GRAY, RESET).arg(m_atrData.ats_sfgi);
        }
        info += QString("%1Features:%2 CID=%3, NAD=%4\n")
            .arg(GRAY, RESET)
            .arg(m_atrData.ats_supportsCID ? "yes" : "no")
            .arg(m_atrData.ats_supportsNAD ? "yes" : "no");
    }

    return info;
}

QString ATRParser::cardTypeToString(CardType type)
{
    switch (type) {
        case CardType::BankCard_EMV: return "Банковская карта EMV";
        case CardType::Mifare_Classic: return "Mifare Classic";
        case CardType::Mifare_DESFire: return "Mifare DESFire";
        case CardType::Mifare_Ultralight: return "Mifare Ultralight";
        case CardType::Mifare_Plus: return "Mifare Plus";
        case CardType::ISO14443A: return "ISO 14443-A";
        case CardType::ISO14443B: return "ISO 14443-B";
        default: return "Неизвестная";
    }
}

QString ATRParser::getFormattedOutput()
{
    QString output;
    
    // Определяем цвет в зависимости от типа карты
    QString cardColor = "#2196F3"; // Синий по умолчанию
    if (m_atrData.cardType == CardType::BankCard_EMV) {
        cardColor = "#4CAF50"; // Зеленый для банковских
    } else if (m_atrData.cardType >= CardType::Mifare_Classic && 
               m_atrData.cardType <= CardType::Mifare_Plus) {
        cardColor = "#FF9800"; // Оранжевый для Mifare
    }
    
    // Заголовок с названием карты
    output += QString("<div style='background: linear-gradient(90deg, %1, %2); padding: 15px; margin: 10px 0; border-radius: 8px;'>")
        .arg(cardColor)
        .arg(cardColor + "CC");
    output += QString("<h2 style='color: white; margin: 0; text-align: center;'>🔖 %1</h2>")
        .arg(m_atrData.cardName);
    output += "</div>";
    
    // Основная информация
    output += "<div style='background: #f5f5f5; padding: 12px; margin: 10px 0; border-left: 4px solid #2196F3;'>";
    output += QString("<b style='color: #1976D2;'>Тип карты:</b> <span style='color: #424242;'>%1</span><br>")
        .arg(cardTypeToString(m_atrData.cardType));
    output += QString("<b style='color: #1976D2;'>Производитель:</b> <span style='color: #424242;'>%1</span>")
        .arg(m_atrData.manufacturer);
    output += "</div>";
    
    // ATR в hex
    output += "<div style='margin: 15px 0;'>";
    output += "<h3 style='color: #1976D2; border-bottom: 2px solid #2196F3; padding-bottom: 5px;'>📋 ATR (HEX)</h3>";
    output += "<div style='background: #263238; padding: 12px; border-radius: 4px; font-family: \"Courier New\", monospace;'>";
    
    QString atrHex;
    for (int i = 0; i < m_atrData.rawAtr.size(); i++) {
        if (i > 0 && i % 16 == 0) atrHex += "<br>";
        else if (i > 0) atrHex += " ";
        
        // Подсветка разных частей ATR
        QString byteColor = "#00E676"; // Зеленый по умолчанию
        if (i == 0) byteColor = "#FF5252"; // TS - красный
        else if (i == 1) byteColor = "#FFD740"; // T0 - желтый
        else if (i >= 2 && i < 2 + m_atrData.interfaceBytes.size()) byteColor = "#00B0FF"; // Interface - голубой
        
        atrHex += QString("<span style='color: %1;'>%2</span>")
            .arg(byteColor)
            .arg(QString("%1").arg(m_atrData.rawAtr[i], 2, 16, QChar('0')).toUpper());
    }
    output += atrHex;
    output += "</div></div>";
    
    // Детальный разбор
    output += "<h3 style='color: #1976D2; border-bottom: 2px solid #2196F3; padding-bottom: 5px; margin-top: 20px;'>🔍 ДЕТАЛЬНЫЙ РАЗБОР ATR</h3>";
    
    // TS байт
    QString tsDescr = (m_atrData.ts == 0x3B) ? "Прямая конвенция" : "Обратная конвенция";
    output += "<div style='background: #FFEBEE; padding: 10px; margin: 8px 0; border-left: 4px solid #F44336;'>";
    output += QString("<b style='color: #C62828;'>TS</b> = <code style='background: #fff; padding: 2px 6px; border-radius: 3px;'>0x%1</code> <span style='color: #666;'>(%2)</span>")
        .arg(m_atrData.ts, 2, 16, QChar('0')).toUpper()
        .arg(tsDescr);
    output += "</div>";
    
    // T0 байт
    int histCount = m_atrData.t0 & 0x0F;
    bool hasTA = (m_atrData.t0 & 0x10) != 0;
    bool hasTB = (m_atrData.t0 & 0x20) != 0;
    bool hasTC = (m_atrData.t0 & 0x40) != 0;
    bool hasTD = (m_atrData.t0 & 0x80) != 0;
    
    output += "<div style='background: #FFF9C4; padding: 10px; margin: 8px 0; border-left: 4px solid #FBC02D;'>";
    output += QString("<b style='color: #F57F17;'>T0</b> = <code style='background: #fff; padding: 2px 6px; border-radius: 3px;'>0x%1</code>")
        .arg(m_atrData.t0, 2, 16, QChar('0')).toUpper();
    output += QString(" <span style='color: #666;'>→ Исторических байт: <b>%1</b>, TA:<b>%2</b> TB:<b>%3</b> TC:<b>%4</b> TD:<b>%5</b></span>")
        .arg(histCount)
        .arg(hasTA ? "✓" : "✗")
        .arg(hasTB ? "✓" : "✗")
        .arg(hasTC ? "✓" : "✗")
        .arg(hasTD ? "✓" : "✗");
    output += "</div>";
    
    // Interface bytes TA
    if (!m_atrData.interfaceDetails.ta.values.isEmpty()) {
        output += "<div style='margin: 15px 0;'>";
        output += "<h4 style='color: #0288D1; margin: 10px 0;'>⚡ INTERFACE BYTES TA (Параметры скорости)</h4>";
        for (int i = 0; i < m_atrData.interfaceDetails.ta.values.size(); i++) {
            uint8_t ta = m_atrData.interfaceDetails.ta.values[i];
            output += "<div style='background: #E1F5FE; padding: 8px; margin: 5px 0; border-left: 3px solid #0288D1;'>";
            output += QString("<b style='color: #01579B;'>TA%1</b> = <code style='background: #fff; padding: 2px 6px; border-radius: 3px;'>0x%2</code>")
                .arg(i+1)
                .arg(ta, 2, 16, QChar('0')).toUpper();
            
            if (i == 0) {
                output += QString(" <span style='color: #666;'>→ Fi=<b>%1</b>, Di=<b>%2</b>, Скорость: <b style='color: #0288D1;'>%3 бит/с</b></span>")
                    .arg(m_atrData.interfaceDetails.ta.clockRateConversion)
                    .arg(m_atrData.interfaceDetails.ta.bitRateAdjustment)
                    .arg(m_atrData.interfaceDetails.ta.baudRate);
            }
            output += "</div>";
        }
        output += "</div>";
    }
    
    // Interface bytes TB
    if (!m_atrData.interfaceDetails.tb.values.isEmpty()) {
        output += "<div style='margin: 15px 0;'>";
        output += "<h4 style='color: #7B1FA2; margin: 10px 0;'>🔋 INTERFACE BYTES TB (Параметры программирования)</h4>";
        for (int i = 0; i < m_atrData.interfaceDetails.tb.values.size(); i++) {
            uint8_t tb = m_atrData.interfaceDetails.tb.values[i];
            output += "<div style='background: #F3E5F5; padding: 8px; margin: 5px 0; border-left: 3px solid #7B1FA2;'>";
            output += QString("<b style='color: #4A148C;'>TB%1</b> = <code style='background: #fff; padding: 2px 6px; border-radius: 3px;'>0x%2</code>")
                .arg(i+1)
                .arg(tb, 2, 16, QChar('0')).toUpper();
            
            if (i == 0) {
                output += QString(" <span style='color: #666;'>→ VPP=<b>%1</b>, IPP=<b>%2</b></span>")
                    .arg(m_atrData.interfaceDetails.tb.programmingVoltage)
                    .arg(m_atrData.interfaceDetails.tb.programmingCurrent);
            }
            output += "</div>";
        }
        output += "</div>";
    }
    
    // Interface bytes TC
    if (!m_atrData.interfaceDetails.tc.values.isEmpty()) {
        output += "<div style='margin: 15px 0;'>";
        output += "<h4 style='color: #E64A19; margin: 10px 0;'>⏱️ INTERFACE BYTES TC (Временные параметры)</h4>";
        for (int i = 0; i < m_atrData.interfaceDetails.tc.values.size(); i++) {
            uint8_t tc = m_atrData.interfaceDetails.tc.values[i];
            output += "<div style='background: #FBE9E7; padding: 8px; margin: 5px 0; border-left: 3px solid #E64A19;'>";
            output += QString("<b style='color: #BF360C;'>TC%1</b> = <code style='background: #fff; padding: 2px 6px; border-radius: 3px;'>0x%2</code>")
                .arg(i+1)
                .arg(tc, 2, 16, QChar('0')).toUpper();
            
            if (i == 0) {
                output += QString(" <span style='color: #666;'>→ Guard Time: <b>%1</b></span>")
                    .arg(m_atrData.interfaceDetails.tc.guardTime);
            } else if (i == 1) {
                output += QString(" <span style='color: #666;'>→ Waiting Time: <b>%1</b></span>")
                    .arg(m_atrData.interfaceDetails.tc.waitingTime);
            }
            output += "</div>";
        }
        output += "</div>";
    }
    
    // Interface bytes TD
    if (!m_atrData.interfaceDetails.td.values.isEmpty()) {
        output += "<div style='margin: 15px 0;'>";
        output += "<h4 style='color: #00796B; margin: 10px 0;'>🔗 INTERFACE BYTES TD (Индикаторы протокола)</h4>";
        for (int i = 0; i < m_atrData.interfaceDetails.td.values.size(); i++) {
            uint8_t td = m_atrData.interfaceDetails.td.values[i];
            output += "<div style='background: #E0F2F1; padding: 8px; margin: 5px 0; border-left: 3px solid #00796B;'>";
            output += QString("<b style='color: #004D40;'>TD%1</b> = <code style='background: #fff; padding: 2px 6px; border-radius: 3px;'>0x%2</code>")
                .arg(i+1)
                .arg(td, 2, 16, QChar('0')).toUpper();
            output += QString(" <span style='color: #666;'>→ Протокол: <b style='color: #00796B;'>T=%1</b></span>")
                .arg(m_atrData.interfaceDetails.td.protocols[i]);
            output += "</div>";
        }
        output += "</div>";
    }
    
    // Исторические байты
    if (!m_atrData.historicalBytes.isEmpty()) {
        output += "<div style='margin: 15px 0;'>";
        output += QString("<h4 style='color: #5D4037; margin: 10px 0;'>📚 ИСТОРИЧЕСКИЕ БАЙТЫ (%1 байт)</h4>")
            .arg(m_atrData.historicalBytes.size());
        output += "<div style='background: #EFEBE9; padding: 12px; border-left: 4px solid #5D4037; font-family: \"Courier New\", monospace;'>";
        
        QString histHex;
        for (int i = 0; i < m_atrData.historicalBytes.size(); i++) {
            if (i > 0 && i % 16 == 0) histHex += "<br>";
            else if (i > 0) histHex += " ";
            histHex += QString("<span style='color: #3E2723;'>%1</span>")
                .arg(QString("%1").arg(m_atrData.historicalBytes[i], 2, 16, QChar('0')).toUpper());
        }
        output += histHex;
        output += "</div></div>";
    }
    
    // TCK (контрольная сумма)
    if (m_atrData.hasTck) {
        bool checksumOk = verifyChecksum();
        QString bgColor = checksumOk ? "#E8F5E9" : "#FFEBEE";
        QString borderColor = checksumOk ? "#4CAF50" : "#F44336";
        QString textColor = checksumOk ? "#2E7D32" : "#C62828";
        QString statusIcon = checksumOk ? "✅" : "❌";
        QString statusText = checksumOk ? "Верна" : "Ошибка";
        
        output += QString("<div style='background: %1; padding: 10px; margin: 10px 0; border-left: 4px solid %2;'>")
            .arg(bgColor).arg(borderColor);
        output += QString("<b style='color: %1;'>TCK</b> = <code style='background: #fff; padding: 2px 6px; border-radius: 3px;'>0x%2</code>")
            .arg(textColor)
            .arg(m_atrData.tck, 2, 16, QChar('0')).toUpper();
        output += QString(" <span style='color: #666;'>→ Контрольная сумма: <b style='color: %1;'>%2 %3</b></span>")
            .arg(textColor)
            .arg(statusIcon)
            .arg(statusText);
        output += "</div>";
    }
    
    // Поддерживаемые протоколы
    if (!m_atrData.supportedProtocols.isEmpty()) {
        output += "<div style='background: #E3F2FD; padding: 10px; margin: 10px 0; border-left: 4px solid #1976D2;'>";
        output += "<b style='color: #0D47A1;'>📡 Поддерживаемые протоколы:</b> ";
        QStringList protoList;
        for (int proto : m_atrData.supportedProtocols) {
            protoList << QString("<span style='background: #1976D2; color: white; padding: 2px 8px; border-radius: 3px; margin: 0 2px;'>T=%1</span>")
                .arg(proto);
        }
        output += protoList.join(" ");
        output += "</div>";
    }
    auto esc = [](const QString &t){ return t.toHtmlEscaped(); };
    auto hex = [](const QVector<uint8_t>& v) {
        QString s; s.reserve(v.size()*3);
        for (int i=0;i<v.size();++i){ s+=QString::asprintf("%02X", v[i]); if(i+1<v.size()) s+=' '; }
        return s;
    };

    // ATS (в том же стиле)
    if (m_atrData.hasATS && !m_atrData.atsRaw.isEmpty()) {
        output += "<div style='margin-top:10px; color:#00BCD4; font-weight:600;'>ATS (ISO/IEC 14443-4)</div>";
        output += "<div><span style='color:#8E24AA;'>ATS:</span> "
               "<span style='color:#222;'>" + esc(hex(m_atrData.atsRaw)) + "</span></div>";

        if (m_atrData.ats_fscPresent) {
            output += "<div><span style='color:#43A047;'>FSC:</span> "
                   "<span style='color:#222;'>" + esc(QString::number(m_atrData.ats_fsc)) + " байт</span></div>";
        }
        if (m_atrData.ats_fwi >= 0) {
            output += "<div><span style='color:#777;'>FWI:</span> "
                   "<span style='color:#222;'>" + esc(QString::number(m_atrData.ats_fwi)) + "</span>"
                   "<span style='color:#777;'> &nbsp; (~timeout)≈</span>"
                   "<span style='color:#222;'>302µs * 2^" + esc(QString::number(m_atrData.ats_fwi)) + "</span></div>";
        }
        if (m_atrData.ats_sfgi >= 0) {
            output += "<div><span style='color:#777;'>SFGI:</span> "
                   "<span style='color:#222;'>" + esc(QString::number(m_atrData.ats_sfgi)) + "</span>"
                   "<span style='color:#777;'> &nbsp; (~guard)≈</span>"
                   "<span style='color:#222;'>302µs * 2^" + esc(QString::number(m_atrData.ats_sfgi)) + "</span></div>";
        }

        output += "<div><span style='color:#777;'>Опции:</span> "
               "<span style='color:#222;'>CID=" + QString(m_atrData.ats_supportsCID ? "да" : "нет") +
               ", NAD=" + QString(m_atrData.ats_supportsNAD ? "да" : "нет") + "</span></div>";

        if (m_atrData.ats_hbLen > 0) {
            output += "<div><span style='color:#777;'>ATS historical bytes:</span> "
                   "<span style='color:#222;'>" + esc(QString::number(m_atrData.ats_hbLen)) + " байт</span></div>";
        }
    }

    output += "</div>"; // wrapper

    return output;
}

void ATRParser::initKnownATRs()
{
    // Добавляем известные ATR карт
    // Формат: ATR строка -> (Тип карты, Название)

    // Mifare Classic 1K
    m_knownATRs["3B 8F 80 01 80 4F 0C A0 00 00 03 06 03 00 01 00 00 00 00 6A"] =
        qMakePair(CardType::Mifare_Classic, "Mifare Classic 1K");

    // Mifare Classic 4K
    m_knownATRs["3B 8F 80 01 80 4F 0C A0 00 00 03 06 03 00 02 00 00 00 00 69"] =
        qMakePair(CardType::Mifare_Classic, "Mifare Classic 4K");

    // Mifare DESFire EV1
    m_knownATRs["3B 81 80 01 80 80"] =
        qMakePair(CardType::Mifare_DESFire, "Mifare DESFire EV1");

    // Mifare Ultralight
    m_knownATRs["3B 8F 80 01 80 4F 0C A0 00 00 03 06 03 00 03 00 00 00 00 68"] =
        qMakePair(CardType::Mifare_Ultralight, "Mifare Ultralight");
}
int ATRParser::atsFSCItoFSC(int fsci)
{
    // ISO/IEC 14443-4: FSCI (0..8,9..C..) → FSC (байт)
    // Наиболее распространенные значения:
    // 0:16, 1:24, 2:32, 3:40, 4:48, 5:64, 6:96, 7:128, 8:256
    static const int map[] = {16,24,32,40,48,64,96,128,256};
    if (fsci >= 0 && fsci <= 8) return map[fsci];
    return -1;
}

bool ATRParser::parseATS(const QVector<uint8_t>& ats)
{
    return parseATS(ats.data(), static_cast<size_t>(ats.size()));
}

bool ATRParser::parseATS(const uint8_t* ats, size_t length)
{
    m_atrData.hasATS = false;
    m_atrData.atsRaw.clear();
    m_atrData.ats_hbLen = -1;
    m_atrData.ats_fscPresent = false;
    m_atrData.ats_fsc = -1;
    m_atrData.ats_taPresent = false;
    m_atrData.ats_tbPresent = false;
    m_atrData.ats_tcPresent = false;
    m_atrData.ats_tdPresent = false;
    m_atrData.ats_fwi = -1;
    m_atrData.ats_sfgi = -1;
    m_atrData.ats_supportsCID = false;
    m_atrData.ats_supportsNAD = false;

    if (!ats || length < 1) {
        emit parsingError(QStringLiteral("ATS пуст или некорректной длины"));
        return false;
    }

    // TL — первый байт, общая длина ATS
    const int TL = ats[0];
    if (TL < 1 || static_cast<size_t>(TL) > length) {
        emit parsingError(QStringLiteral("ATS: некорректная длина TL"));
        return false;
    }

    m_atrData.atsRaw = QVector<uint8_t>(ats, ats + TL);
    m_atrData.hasATS = true;

    if (TL < 2) {
        // только TL — крайне редко, но считаем валидным
        return true;
    }

    // T0 (или форматный байт ATS для 14443-4)
    const uint8_t T0 = ats[1];
    const int hbLen = T0 & 0x0F;         // исторические байты в ATS
    const bool TA_present = (T0 & 0x10) != 0;
    const bool TB_present = (T0 & 0x20) != 0;
    const bool TC_present = (T0 & 0x40) != 0;
    const bool TD_present = (T0 & 0x80) != 0;

    m_atrData.ats_hbLen = hbLen;
    m_atrData.ats_taPresent = TA_present;
    m_atrData.ats_tbPresent = TB_present;
    m_atrData.ats_tcPresent = TC_present;
    m_atrData.ats_tdPresent = TD_present;

    int idx = 2;

    // TA(ATS) — FSCI (низкие 4 бита)
    if (TA_present && idx < TL) {
        uint8_t TA = ats[idx++];
        int fsci = TA & 0x0F;
        m_atrData.ats_fscPresent = true;
        m_atrData.ats_fsc = atsFSCItoFSC(fsci);
    }

    // TB(ATS) — FWI (высокие 4 бита), SFGI (низкие 4 бита)
    if (TB_present && idx < TL) {
        uint8_t TB = ats[idx++];
        m_atrData.ats_fwi = (TB >> 4) & 0x0F;
        m_atrData.ats_sfgi = TB & 0x0F;
    }

    // TC(ATS) — поддержка NAD/CID
    if (TC_present && idx < TL) {
        uint8_t TC = ats[idx++];
        m_atrData.ats_supportsCID = (TC & 0x02) != 0;
        m_atrData.ats_supportsNAD = (TC & 0x01) != 0;
    }

    // TD(ATS) — редко используется, пропустим как необязательный байт
    if (TD_present && idx < TL) {
        ++idx; // зарезервировано или пропустить расширения
    }

    // Остаток — исторические байты ATS (если hbLen > 0)
    // Убедимся, что места достаточно
    if (hbLen > 0 && idx + hbLen <= TL) {
        // Можно при необходимости сохранить отдельно — пока используем в atsRaw
    }

    return true;
}

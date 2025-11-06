#include "atrparser.h"
#include <QDebug>

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
    const int boxWidth = 76;

    // Вспомогательная функция для создания строки с выравниванием
    auto makeLine = [boxWidth](const QString &label, const QString &value) -> QString {
        QString line = "║ " + label + ": ";
        int padding = boxWidth - 4 - line.length() - value.length();
        if (padding < 0) padding = 0;
        line += value + QString(padding, ' ') + " ║\n";
        return line;
    };

    // Заголовок
    output += QString("╔%1╗\n").arg(QString().fill(QChar(0x2550), boxWidth - 2));
    QString title = m_atrData.cardName;
    int titlePadding = (boxWidth - 2 - title.length()) / 2;
    output += QString("║%1%2%3║\n")
        .arg(QString(titlePadding, ' '))
        .arg(title)
        .arg(QString(boxWidth - 2 - titlePadding - title.length(), ' '));
    output += QString("╠%1╣\n").arg(QString().fill(QChar(0x2550), boxWidth - 2));

    // Основная информация
    output += makeLine("Тип", cardTypeToString(m_atrData.cardType));
    output += makeLine("Производитель", m_atrData.manufacturer);

    output += QString("╠%1╣\n").arg(QString().fill(QChar(0x2550), boxWidth - 2));

    // ATR в hex
    QString atrHex;
    for (int i = 0; i < m_atrData.rawAtr.size(); i++) {
        if (i > 0 && i % 16 == 0) atrHex += "\n";
        else if (i > 0) atrHex += " ";
        atrHex += QString("%1").arg(m_atrData.rawAtr[i], 2, 16, QChar('0')).toUpper();
    }

    output += "║ ATR (HEX):                                                           ║\n";
    QStringList atrLines = atrHex.split('\n');
    for (const QString &line : atrLines) {
        output += QString("║  %1%2║\n")
            .arg(line)
            .arg(QString(boxWidth - 4 - line.length(), ' '));
    }

    output += QString("╠%1╣\n").arg(QString().fill(QChar(0x2550), boxWidth - 2));
    output += "║ ДЕТАЛЬНЫЙ РАЗБОР ATR:                                                ║\n";
    output += QString("╟%1╢\n").arg(QString().fill(QChar(0x2500), boxWidth - 2));

    // TS байт
    QString tsDescr = (m_atrData.ts == 0x3B) ? "Прямая конвенция" : "Обратная конвенция";
    output += QString("║  TS = 0x%1 (%2)%3║\n")
        .arg(m_atrData.ts, 2, 16, QChar('0')).toUpper()
        .arg(tsDescr)
        .arg(QString(boxWidth - 17 - tsDescr.length(), ' '));

    // T0 байт
    int histCount = m_atrData.t0 & 0x0F;
    bool hasTA = (m_atrData.t0 & 0x10) != 0;
    bool hasTB = (m_atrData.t0 & 0x20) != 0;
    bool hasTC = (m_atrData.t0 & 0x40) != 0;
    bool hasTD = (m_atrData.t0 & 0x80) != 0;

    output += QString("║  T0 = 0x%1 (К-во ист.байт: %2, TA:%3 TB:%4 TC:%5 TD:%6)%7║\n")
        .arg(m_atrData.t0, 2, 16, QChar('0')).toUpper()
        .arg(histCount, 2)
        .arg(hasTA ? "1" : "0")
        .arg(hasTB ? "1" : "0")
        .arg(hasTC ? "1" : "0")
        .arg(hasTD ? "1" : "0")
        .arg(QString(boxWidth - 53, ' '));

    // Interface bytes TA
    if (!m_atrData.interfaceDetails.ta.values.isEmpty()) {
        output += QString("╟%1╢\n").arg(QString().fill(QChar(0x2500), boxWidth - 2));
        output += "║ INTERFACE BYTES TA (Параметры скорости):                         ║\n";
        for (int i = 0; i < m_atrData.interfaceDetails.ta.values.size(); i++) {
            uint8_t ta = m_atrData.interfaceDetails.ta.values[i];
            QString taLine = QString("  TA%1 = 0x%2").arg(i+1).arg(ta, 2, 16, QChar('0')).toUpper();

            if (i == 0) {
                taLine += QString(" → Fi=%1, Di=%2, Скорость: %3 бит/с")
                    .arg(m_atrData.interfaceDetails.ta.clockRateConversion)
                    .arg(m_atrData.interfaceDetails.ta.bitRateAdjustment)
                    .arg(m_atrData.interfaceDetails.ta.baudRate);
            }
            output += QString("║%1%2║\n").arg(taLine).arg(QString(boxWidth - 2 - taLine.length(), ' '));
        }
    }

    // Interface bytes TB
    if (!m_atrData.interfaceDetails.tb.values.isEmpty()) {
        output += QString("╟%1╢\n").arg(QString().fill(QChar(0x2500), boxWidth - 2));
        output += "║ INTERFACE BYTES TB (Параметры программирования):                 ║\n";
        for (int i = 0; i < m_atrData.interfaceDetails.tb.values.size(); i++) {
            uint8_t tb = m_atrData.interfaceDetails.tb.values[i];
            QString tbLine = QString("  TB%1 = 0x%2").arg(i+1).arg(tb, 2, 16, QChar('0')).toUpper();

            if (i == 0) {
                tbLine += QString(" → VPP=%1, IPP=%2")
                    .arg(m_atrData.interfaceDetails.tb.programmingVoltage)
                    .arg(m_atrData.interfaceDetails.tb.programmingCurrent);
            }
            output += QString("║%1%2║\n").arg(tbLine).arg(QString(boxWidth - 2 - tbLine.length(), ' '));
        }
    }

    // Interface bytes TC
    if (!m_atrData.interfaceDetails.tc.values.isEmpty()) {
        output += QString("╟%1╢\n").arg(QString().fill(QChar(0x2500), boxWidth - 2));
        output += "║ INTERFACE BYTES TC (Временные параметры):                        ║\n";
        for (int i = 0; i < m_atrData.interfaceDetails.tc.values.size(); i++) {
            uint8_t tc = m_atrData.interfaceDetails.tc.values[i];
            QString tcLine = QString("  TC%1 = 0x%2").arg(i+1).arg(tc, 2, 16, QChar('0')).toUpper();

            if (i == 0) {
                tcLine += QString(" → Guard Time: %1").arg(m_atrData.interfaceDetails.tc.guardTime);
            } else if (i == 1) {
                tcLine += QString(" → Waiting Time: %1").arg(m_atrData.interfaceDetails.tc.waitingTime);
            }
            output += QString("║%1%2║\n").arg(tcLine).arg(QString(boxWidth - 2 - tcLine.length(), ' '));
        }
    }

    // Interface bytes TD
    if (!m_atrData.interfaceDetails.td.values.isEmpty()) {
        output += QString("╟%1╢\n").arg(QString().fill(QChar(0x2500), boxWidth - 2));
        output += "║ INTERFACE BYTES TD (Индикаторы протокола):                       ║\n";
        for (int i = 0; i < m_atrData.interfaceDetails.td.values.size(); i++) {
            uint8_t td = m_atrData.interfaceDetails.td.values[i];
            QString tdLine = QString("  TD%1 = 0x%2 → Протокол: T=%3")
                .arg(i+1)
                .arg(td, 2, 16, QChar('0')).toUpper()
                .arg(m_atrData.interfaceDetails.td.protocols[i]);
            output += QString("║%1%2║\n").arg(tdLine).arg(QString(boxWidth - 2 - tdLine.length(), ' '));
        }
    }

    // Исторические байты
    if (!m_atrData.historicalBytes.isEmpty()) {
        output += QString("╟%1╢\n").arg(QString().fill(QChar(0x2500), boxWidth - 2));
        output += QString("║ ИСТОРИЧЕСКИЕ БАЙТЫ (%1 байт):                                  ║\n")
            .arg(m_atrData.historicalBytes.size());

        QString histHex;
        for (int i = 0; i < m_atrData.historicalBytes.size(); i++) {
            if (i > 0 && i % 16 == 0) histHex += "\n";
            else if (i > 0) histHex += " ";
            histHex += QString("%1").arg(m_atrData.historicalBytes[i], 2, 16, QChar('0')).toUpper();
        }

        QStringList histLines = histHex.split('\n');
        for (const QString &line : histLines) {
            output += QString("║  %1%2║\n")
                .arg(line)
                .arg(QString(boxWidth - 4 - line.length(), ' '));
        }
    }

    // TCK (контрольная сумма)
    if (m_atrData.hasTck) {
        output += QString("╟%1╢\n").arg(QString().fill(QChar(0x2500), boxWidth - 2));
        bool checksumOk = verifyChecksum();
        QString checksumStatus = checksumOk ? "✓ Верна" : "✗ Ошибка";
        QString tckLine = QString("  TCK = 0x%1 (Контрольная сумма: %2)")
            .arg(m_atrData.tck, 2, 16, QChar('0')).toUpper()
            .arg(checksumStatus);
        output += QString("║%1%2║\n").arg(tckLine).arg(QString(boxWidth - 2 - tckLine.length(), ' '));
    }

    // Поддерживаемые протоколы
    if (!m_atrData.supportedProtocols.isEmpty()) {
        output += QString("╟%1╢\n").arg(QString().fill(QChar(0x2500), boxWidth - 2));
        QString protocols;
        for (int proto : m_atrData.supportedProtocols) {
            if (!protocols.isEmpty()) protocols += ", ";
            protocols += QString("T=%1").arg(proto);
        }
        QString protoLine = QString("  Поддерживаемые протоколы: %1").arg(protocols);
        output += QString("║%1%2║\n").arg(protoLine).arg(QString(boxWidth - 2 - protoLine.length(), ' '));
    }

    output += QString("╚%1╝\n").arg(QString().fill(QChar(0x2550), boxWidth - 2));

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

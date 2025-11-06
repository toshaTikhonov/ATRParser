# ATR Parser для банковских и Mifare карт

Парсер ATR (Answer To Reset) для работы с пластиковыми картами через PC/SC Lite.
Поддерживает определение банковских EMV карт и Mifare карт (Classic, DESFire, Ultralight, Plus).

## Возможности

- ✅ Парсинг ATR с полной структурой (TS, T0, interface bytes, historical bytes, TCK)
- ✅ Определение типов карт: банковские EMV, Mifare Classic/DESFire/Ultralight/Plus
- ✅ Определение производителей карт (Visa, Mastercard, American Express, NXP и др.)
- ✅ Автоматический мониторинг вставки/извлечения карт
- ✅ GUI и консольный интерфейс
- ✅ Поддержка множественных ридеров
- ✅ Проверка контрольной суммы ATR

## Требования

### Linux (Ubuntu/Debian)
```bash
sudo apt-get install libpcsclite-dev pcscd pcsc-tools
sudo apt-get install qt6-base-dev  # или qt5-default для Qt5
```

### macOS
```bash
brew install pcsc-lite
brew install qt@6  # или qt@5
```

### Windows
- Установите [PC/SC драйвер](https://www.microsoft.com/en-us/download/)
- Установите Qt 5 или Qt 6

## Сборка

### С использованием CMake (рекомендуется)

```bash
mkdir build
cd build
cmake ..
make
```

Собранные бинарники:
- `atrparser_gui` - GUI приложение
- `atrparser_console` - консольное приложение

### С использованием qmake

```bash
# Для GUI приложения
qmake atrparser_gui.pro
make

# Для консольного приложения
qmake atrparser_console.pro
make
```

## Использование

### Запуск PC/SC службы (Linux)

```bash
# Запуск службы
sudo systemctl start pcscd

# Проверка статуса
sudo systemctl status pcscd

# Проверка подключенных ридеров
pcsc_scan
```

### GUI приложение

```bash
./atrparser_gui
```

Интерфейс позволяет:
1. Выбрать ридер из списка
2. Подключиться к ридеру
3. Прочитать карту вручную
4. Включить автоматический мониторинг карт

### Консольное приложение

```bash
./atrparser_console
```

Автоматически:
1. Находит доступные ридеры
2. Подключается к первому ридеру
3. Пытается прочитать карту
4. Запускает мониторинг вставки/извлечения карт

## Примеры использования в коде

### Базовое использование парсера ATR

```cpp
#include "atrparser.h"

// Создание парсера
ATRParser parser;

// ATR в виде массива байтов
QVector<uint8_t> atr = {0x3B, 0x8F, 0x80, 0x01, 0x80, 0x4F, 
                        0x0C, 0xA0, 0x00, 0x00, 0x03, 0x06};

// Парсинг
if (parser.parseATR(atr)) {
    ATRData data = parser.getATRData();
    
    qDebug() << "Тип карты:" << data.cardName;
    qDebug() << "Производитель:" << data.manufacturer;
    qDebug() << "ATR:" << parser.atrToString();
}
```

### Работа с ридером

```cpp
#include "cardreader.h"

// Создание ридера
CardReader reader;

// Инициализация
if (reader.initialize()) {
    // Получение списка ридеров
    QStringList readers = reader.listReaders();
    
    // Подключение к первому ридеру
    if (!readers.isEmpty() && reader.connectToReader(readers[0])) {
        // Чтение информации о карте
        ATRData cardInfo = reader.readCardInfo();
        
        qDebug() << "Карта:" << cardInfo.cardName;
        qDebug() << "Тип:" << ATRParser::cardTypeToString(cardInfo.cardType);
    }
}
```

### Мониторинг карт с сигналами

```cpp
CardReader reader;

// Подключение сигналов
QObject::connect(&reader, &CardReader::cardInserted, 
    [](const ATRData &cardInfo) {
        qDebug() << "Карта вставлена:" << cardInfo.cardName;
    });

QObject::connect(&reader, &CardReader::cardRemoved, 
    []() {
        qDebug() << "Карта извлечена";
    });

// Запуск мониторинга (проверка каждые 500 мс)
reader.connectToReader("название ридера");
reader.startMonitoring(500);
```

## Поддерживаемые типы карт

### Банковские карты (EMV)
- Visa (RID: A0 00 00 00 03)
- Mastercard (RID: A0 00 00 00 04)
- American Express (RID: A0 00 00 00 25)
- Другие EMV совместимые карты

### Mifare карты
- **Mifare Classic 1K/4K** - память 1KB или 4KB, используется в транспорте и СКУД
- **Mifare DESFire** - безопасная карта с шифрованием DES/3DES/AES
- **Mifare Ultralight** - простая карта для одноразового использования
- **Mifare Plus** - улучшенная версия Classic с повышенной безопасностью

### Другие
- ISO 14443-A карты
- ISO 14443-B карты

## Структура ATR

ATR (Answer To Reset) состоит из:

```
TS (1 байт)      - Initial character (0x3B или 0x3F)
T0 (1 байт)      - Format character
TA, TB, TC, TD   - Interface bytes (опционально)
Historical bytes - Информация о карте (0-15 байтов)
TCK (1 байт)     - Checksum (опционально, для T≠0)
```

## API Reference

### ATRParser

```cpp
// Основные методы
bool parseATR(const QVector<uint8_t> &atr);
ATRData getATRData() const;
CardType getCardType() const;
QString getCardName() const;
QString atrToString() const;
QString getDetailedInfo() const;

// Статические методы
static QString cardTypeToString(CardType type);

// Сигналы
void cardDetected(CardType type, const QString &name);
void parsingError(const QString &error);
```

### CardReader

```cpp
// Инициализация
bool initialize();
void cleanup();

// Управление ридерами
QStringList listReaders();
bool connectToReader(const QString &readerName);
void disconnect();

// Чтение карт
QVector<uint8_t> getATR();
ATRData readCardInfo();

// Мониторинг
void startMonitoring(int intervalMs = 1000);
void stopMonitoring();

// Сигналы
void cardInserted(const ATRData &cardInfo);
void cardRemoved();
void readerError(const QString &error);
void readersListChanged(const QStringList &readers);
```

### ATRData структура

```cpp
struct ATRData {
    QVector<uint8_t> rawAtr;           // Полный ATR
    uint8_t ts;                        // Initial character
    uint8_t t0;                        // Format character
    QVector<uint8_t> interfaceBytes;   // Interface bytes
    QVector<uint8_t> historicalBytes;  // Historical bytes
    uint8_t tck;                       // Checksum
    bool hasTck;                       // Наличие checksum
    
    QVector<int> supportedProtocols;   // Поддерживаемые протоколы (T=0, T=1)
    
    CardType cardType;                 // Тип карты
    QString cardName;                  // Название карты
    QString manufacturer;              // Производитель
};
```

## Устранение неполадок

### PC/SC служба не запускается (Linux)

```bash
# Проверка установки
dpkg -l | grep pcscd

# Переустановка
sudo apt-get remove --purge pcscd libpcsclite1
sudo apt-get install pcscd libpcsclite-dev

# Запуск с логами
sudo pcscd -f -d
```

### Ридер не обнаруживается

```bash
# Проверка USB устройств
lsusb

# Проверка ридеров
pcsc_scan

# Права доступа (добавление пользователя в группу)
sudo usermod -a -G pcscd $USER
```

### Карта не читается

1. Проверьте, что карта правильно вставлена в ридер
2. Попробуйте другую карту для проверки ридера
3. Убедитесь, что карта не повреждена
4. Проверьте логи: `journalctl -u pcscd -f`

## Лицензия

MIT License

## Автор

Разработано с использованием:
- Qt Framework
- PC/SC Lite
- ISO 7816 стандарт
- EMV спецификации

## Дополнительные ресурсы

- [PC/SC Workgroup](https://pcscworkgroup.com/)
- [ISO/IEC 7816](https://en.wikipedia.org/wiki/ISO/IEC_7816)
- [EMV Specifications](https://www.emvco.com/)
- [Mifare Documentation](https://www.nxp.com/products/rfid-nfc/mifare-hf)

# Структура проекта ATR Parser

## Основные компоненты

### 1. Парсер ATR (atrparser.h / atrparser.cpp)
Класс `ATRParser` - основной парсер ATR с функциями:
- Парсинг структуры ATR (TS, T0, interface bytes, historical bytes, TCK)
- Определение типов карт (банковские EMV, Mifare)
- Определение производителей
- Проверка контрольной суммы

### 2. Работа с ридером (cardreader.h / cardreader.cpp)
Класс `CardReader` - обёртка над PC/SC Lite:
- Управление подключением к ридерам
- Чтение ATR с карты
- Автоматический мониторинг вставки/извлечения карт
- Qt сигналы для событий карт

### 3. GUI приложение (main.cpp)
Графический интерфейс на Qt Widgets:
- Список доступных ридеров
- Кнопки управления (подключение, чтение, мониторинг)
- Вывод информации о картах
- Удобный интерфейс для работы

### 4. Консольное приложение (console_example.cpp)
Простая консольная версия:
- Автоматическое подключение
- Красивый вывод в терминале
- Мониторинг карт в реальном времени

## Файлы сборки

- **CMakeLists.txt** - сборка через CMake (рекомендуется)
- **atrparser.pro** - главный проект для qmake
- **atrparser_gui.pro** - GUI приложение для qmake
- **atrparser_console.pro** - консольное приложение для qmake

## Документация

- **README.md** - полная документация на английском
- **QUICKSTART.md** - быстрый старт на русском
- **.gitignore** - исключения для git

## Зависимости

### Библиотеки
- Qt 5.15+ или Qt 6.x (Core, Widgets)
- PC/SC Lite (libpcsclite)

### Система
- Linux: pcscd служба
- macOS: встроенная поддержка PC/SC
- Windows: PC/SC драйвер

## Поддерживаемые карты

### Банковские (EMV)
✓ Visa
✓ Mastercard
✓ American Express
✓ Другие EMV карты

### Mifare
✓ Mifare Classic 1K/4K
✓ Mifare DESFire
✓ Mifare Ultralight
✓ Mifare Plus

### Общие
✓ ISO 14443-A
✓ ISO 14443-B

## Архитектура

```
┌─────────────────────────────────────────────────┐
│              GUI / Console App                  │
├─────────────────────────────────────────────────┤
│            CardReader (Qt wrapper)              │
│  ┌──────────────────┐  ┌──────────────────┐    │
│  │  PC/SC Interface │  │   ATR Parser     │    │
│  │  - List readers  │  │  - Parse ATR     │    │
│  │  - Connect       │  │  - Detect card   │    │
│  │  - Read ATR      │  │  - Verify CRC    │    │
│  │  - Monitor       │  │  - Manufacturer  │    │
│  └──────────────────┘  └──────────────────┘    │
├─────────────────────────────────────────────────┤
│          PC/SC Lite (libpcsclite)               │
├─────────────────────────────────────────────────┤
│              Hardware Reader                    │
└─────────────────────────────────────────────────┘
```

## Использование классов

### Простое чтение карты
```cpp
CardReader reader;
reader.initialize();
reader.connectToReader("ACR122U");
ATRData card = reader.readCardInfo();
qDebug() << card.cardName;
```

### С мониторингом
```cpp
CardReader reader;
connect(&reader, &CardReader::cardInserted, 
    [](const ATRData &card) {
        qDebug() << "Карта:" << card.cardName;
    });

reader.connectToReader("ACR122U");
reader.startMonitoring(500);
```

### Только парсинг ATR
```cpp
ATRParser parser;
QVector<uint8_t> atr = { /* ATR bytes */ };
parser.parseATR(atr);
qDebug() << parser.getCardName();
```

## Расширение функционала

### Добавление нового типа карты

1. Добавьте enum в `atrparser.h`:
```cpp
enum class CardType {
    ...
    NewCardType,
};
```

2. Добавьте метод определения в `atrparser.cpp`:
```cpp
bool ATRParser::isNewCardType() const {
    // Логика определения
}
```

3. Вызовите в `detectCardType()`:
```cpp
else if (isNewCardType()) {
    m_atrData.cardType = CardType::NewCardType;
}
```

### Добавление известного ATR

Добавьте в `initKnownATRs()`:
```cpp
m_knownATRs["3B XX XX ..."] = 
    qMakePair(CardType::Type, "Card Name");
```

## Тестирование

### Проверка ридера
```bash
pcsc_scan
```

### Отправка APDU команд
```bash
opensc-tool --send-apdu 00:A4:04:00:00
```

### Логирование PC/SC
```bash
journalctl -u pcscd -f
```

## Лицензия

MIT License - свободное использование в коммерческих и некоммерческих проектах

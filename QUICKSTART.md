# Быстрый старт - ATR Parser

## Установка зависимостей

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y libpcsclite-dev pcscd pcsc-tools qt6-base-dev cmake build-essential
```

### Fedora/RHEL
```bash
sudo dnf install -y pcsc-lite-devel pcsc-lite qt6-qtbase-devel cmake gcc-c++
```

## Сборка и запуск

```bash
# Клонируйте или скопируйте файлы в папку
cd atrparser

# Сборка
mkdir build && cd build
cmake ..
make -j$(nproc)

# Запуск службы PC/SC (если не запущена)
sudo systemctl start pcscd

# Запуск GUI приложения
./atrparser_gui

# Или консольного
./atrparser_console
```

## Проверка работоспособности ридера

```bash
# Проверка USB устройств
lsusb | grep -i reader

# Проверка PC/SC
pcsc_scan
```

## Тестирование с картами

### Банковская карта (EMV)
1. Подключите ридер
2. Запустите приложение
3. Выберите ридер из списка и нажмите "Подключить"
4. Вставьте банковскую карту
5. Нажмите "Прочитать карту" или включите мониторинг

Результат покажет:
- Тип: Банковская карта (EMV)
- Производитель: Visa/Mastercard/AmEx
- ATR и исторические байты

### Mifare карта
1. Подключите бесконтактный ридер (ACR122U, например)
2. Приложите Mifare карту (транспортная, пропуск и т.д.)
3. Результат покажет тип Mifare карты

## Примеры вывода

### Банковская Visa карта
```
Тип карты:     Банковская карта (EMV)
Производитель: Visa
ATR: 3B 6F 00 00 80 31 E0 6B 04 51 01 02 00 00 00 00 9F
```

### Mifare Classic карта
```
Тип карты:     Mifare Classic
Производитель: Philips/NXP
ATR: 3B 8F 80 01 80 4F 0C A0 00 00 03 06 03 00 01 00 00 00 00 6A
```

## Интеграция в проект

### Минимальный пример
```cpp
#include "cardreader.h"
#include "atrparser.h"
#include <QCoreApplication>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    CardReader reader;
    reader.initialize();
    
    auto readers = reader.listReaders();
    if (!readers.isEmpty()) {
        reader.connectToReader(readers[0]);
        
        ATRData cardInfo = reader.readCardInfo();
        qDebug() << "Карта:" << cardInfo.cardName;
    }
    
    return 0;
}
```

## Частые проблемы

**Проблема**: `Ридеры не найдены`
**Решение**: 
```bash
sudo systemctl restart pcscd
sudo chmod 666 /var/run/pcscd/pcscd.comm
```

**Проблема**: `Permission denied`
**Решение**:
```bash
sudo usermod -a -G pcscd $USER
# Затем перелогиньтесь
```

**Проблема**: Карта не читается
**Решение**: Проверьте, что карта ISO 7816 совместима и правильно вставлена

## Дополнительная информация

- Логи PC/SC: `journalctl -u pcscd -f`
- Тестирование ридера: `opensc-tool --list-readers`
- Отправка APDU команд: `opensc-tool --send-apdu 00:A4:04:00:00`

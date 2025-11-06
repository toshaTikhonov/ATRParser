#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QTextEdit>
#include <QLabel>
#include <QGroupBox>
#include <QMessageBox>

#include "cardreader.h"
#include "atrparser.h"

class CardReaderWindow : public QMainWindow
{
    Q_OBJECT

public:
    CardReaderWindow(QWidget *parent = nullptr)
        : QMainWindow(parent)
    {
        setWindowTitle("ATR Parser - Чтение банковских и Mifare карт");
        setMinimumSize(800, 600);
        
        setupUI();
        setupCardReader();
        
        refreshReaders();
    }

private slots:
    void refreshReaders()
    {
        m_readerCombo->clear();
        QStringList readers = m_cardReader->listReaders();
        
        if (readers.isEmpty()) {
            m_infoText->append("<font color='red'>Ридеры не найдены! Проверьте подключение.</font>");
            return;
        }
        
        m_readerCombo->addItems(readers);
        m_infoText->append(QString("<font color='green'>Найдено ридеров: %1</font>").arg(readers.size()));
        
        for (const QString &reader : readers) {
            m_infoText->append(QString("  • %1").arg(reader));
        }
    }
    
    void connectReader()
    {
        QString readerName = m_readerCombo->currentText();
        if (readerName.isEmpty()) {
            QMessageBox::warning(this, "Ошибка", "Выберите ридер из списка");
            return;
        }
        
        if (m_cardReader->connectToReader(readerName)) {
            m_connectBtn->setEnabled(false);
            m_disconnectBtn->setEnabled(true);
            m_monitorBtn->setEnabled(true);

            m_infoText->append(QString("<b>Подключено к: %1</b>").arg(readerName));
        }
    }
    
    void disconnectReader()
    {
        m_cardReader->disconnect();
        m_cardReader->stopMonitoring();
        
        m_connectBtn->setEnabled(true);
        m_disconnectBtn->setEnabled(false);
        m_monitorBtn->setEnabled(false);
        m_monitorBtn->setText("Начать мониторинг");
        
        m_infoText->append("<b>Отключено от ридера</b>");
    }
    
    void toggleMonitoring()
    {
        if (m_monitorBtn->text() == "Начать мониторинг") {
            m_cardReader->startMonitoring(500);
            m_monitorBtn->setText("Остановить мониторинг");
            m_infoText->append("<font color='blue'>Мониторинг запущен...</font>");
        } else {
            m_cardReader->stopMonitoring();
            m_monitorBtn->setText("Начать мониторинг");
            m_infoText->append("<font color='blue'>Мониторинг остановлен</font>");
        }
    }
    
    void onCardInserted(const ATRData &cardInfo)
    {
        m_infoText->append("\n<b><font color='green'>🔔 КАРТА ОБНАРУЖЕНА!</font></b>");
        displayCardInfo(cardInfo);
    }
    
    void onCardRemoved()
    {
        m_infoText->append("<b><font color='orange'>🔔 Карта извлечена</font></b>\n");
    }
    
    void onReaderError(const QString &error)
    {
        m_infoText->append(QString("<font color='red'>Ошибка: %1</font>").arg(error));
    }

private:
    void setupUI()
    {
        QWidget *centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);
        
        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
        
        // Группа управления ридером
        QGroupBox *readerGroup = new QGroupBox("Управление ридером");
        QVBoxLayout *readerLayout = new QVBoxLayout(readerGroup);
        
        QHBoxLayout *readerSelectLayout = new QHBoxLayout();
        readerSelectLayout->addWidget(new QLabel("Ридер:"));
        m_readerCombo = new QComboBox();
        readerSelectLayout->addWidget(m_readerCombo, 1);
        
        m_refreshBtn = new QPushButton("Обновить список");
        connect(m_refreshBtn, &QPushButton::clicked, this, &CardReaderWindow::refreshReaders);
        readerSelectLayout->addWidget(m_refreshBtn);
        
        readerLayout->addLayout(readerSelectLayout);
        
        QHBoxLayout *readerControlLayout = new QHBoxLayout();
        m_connectBtn = new QPushButton("Подключить");
        connect(m_connectBtn, &QPushButton::clicked, this, &CardReaderWindow::connectReader);
        readerControlLayout->addWidget(m_connectBtn);
        
        m_disconnectBtn = new QPushButton("Отключить");
        m_disconnectBtn->setEnabled(false);
        connect(m_disconnectBtn, &QPushButton::clicked, this, &CardReaderWindow::disconnectReader);
        readerControlLayout->addWidget(m_disconnectBtn);

        m_monitorBtn = new QPushButton("Начать мониторинг");
        m_monitorBtn->setEnabled(false);
        connect(m_monitorBtn, &QPushButton::clicked, this, &CardReaderWindow::toggleMonitoring);
        readerControlLayout->addWidget(m_monitorBtn);
        
        readerLayout->addLayout(readerControlLayout);
        mainLayout->addWidget(readerGroup);
        
        // Текстовое поле вывода информации
        QGroupBox *infoGroup = new QGroupBox("Информация о картах");
        QVBoxLayout *infoLayout = new QVBoxLayout(infoGroup);
        
        m_infoText = new QTextEdit();
        m_infoText->setReadOnly(true);
        m_infoText->setStyleSheet("QTextEdit { font-family: 'Courier New', monospace; }");
        infoLayout->addWidget(m_infoText);
        
        QPushButton *clearBtn = new QPushButton("Очистить");
        connect(clearBtn, &QPushButton::clicked, m_infoText, &QTextEdit::clear);
        infoLayout->addWidget(clearBtn);
        
        mainLayout->addWidget(infoGroup, 1);
    }
    
    void setupCardReader()
    {
        m_cardReader = new CardReader(this);
        
        connect(m_cardReader, &CardReader::cardInserted,
                this, &CardReaderWindow::onCardInserted);
        connect(m_cardReader, &CardReader::cardRemoved,
                this, &CardReaderWindow::onCardRemoved);
        connect(m_cardReader, &CardReader::readerError,
                this, &CardReaderWindow::onReaderError);
        
        if (!m_cardReader->initialize()) {
            QMessageBox::critical(this, "Ошибка",
                "Не удалось инициализировать PC/SC.\n"
                "Убедитесь, что служба pcscd запущена.");
        }
    }
    
    void displayCardInfo(const ATRData &cardInfo)
    {
        // Получаем красиво отформатированный вывод из парсера
        ATRParser parser;
        parser.parseATR(cardInfo.rawAtr);
        if (cardInfo.hasATS)
            parser.parseATS(cardInfo.atsRaw);
        QString formattedOutput = parser.getFormattedOutput();

        // Выводим в текстовое поле с моноширинным шрифтом
        m_infoText->append("<pre>" + formattedOutput + "</pre>");
    }
    
    QComboBox *m_readerCombo;
    QPushButton *m_refreshBtn;
    QPushButton *m_connectBtn;
    QPushButton *m_disconnectBtn;
    QPushButton *m_monitorBtn;
    QTextEdit *m_infoText;
    
    CardReader *m_cardReader;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    CardReaderWindow window;
    window.show();
    
    return app.exec();
}

#include "main.moc"

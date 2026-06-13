#include "AnalyzeFileTime.h"

#include <QtCore/QAbstractTableModel>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFileInfo>
#include <QtCore/QLocale>
#include <QtCore/QSignalBlocker>
#include <QtCore/QtGlobal>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtGui/QPainter>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QStatusBar>

#include <algorithm>

namespace {

struct FileRecord
{
    int originalIndex = 0;
    QString fileName;
    QString absolutePath;
    QDateTime createdTime;
};

QDateTime fileCreatedTime(const QFileInfo &fileInfo)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QDateTime created = fileInfo.birthTime();
    if (created.isValid())
        return created;
#endif

    QDateTime metadataChanged = fileInfo.metadataChangeTime();
    if (metadataChanged.isValid())
        return metadataChanged;

    return fileInfo.lastModified();
}

qint64 absoluteMillisecondsBetween(const QDateTime &left, const QDateTime &right)
{
    if (!left.isValid() || !right.isValid())
        return -1;

    qint64 milliseconds = left.msecsTo(right);
    return milliseconds < 0 ? -milliseconds : milliseconds;
}

QString formatDecimal(double value)
{
    QString text = QString::number(value, 'f', 3);
    while (text.contains('.') && text.endsWith('0'))
        text.chop(1);
    if (text.endsWith('.'))
        text.chop(1);
    return text;
}

double durationValue(qint64 milliseconds, int precisionIndex)
{
    switch (qBound(0, precisionIndex, 3)) {
    case 0:
        return static_cast<double>(milliseconds);
    case 1:
        return static_cast<double>(milliseconds) / 1000.0;
    case 2:
        return static_cast<double>(milliseconds) / 60000.0;
    case 3:
        return static_cast<double>(milliseconds) / 3600000.0;
    default:
        return 0.0;
    }
}

QString durationUnitName(int precisionIndex)
{
    switch (qBound(0, precisionIndex, 3)) {
    case 0:
        return QStringLiteral("毫秒");
    case 1:
        return QStringLiteral("秒");
    case 2:
        return QStringLiteral("分鐘");
    case 3:
        return QStringLiteral("小時");
    default:
        return QString();
    }
}

int compareText(const QString &left, const QString &right)
{
    const int result = QString::localeAwareCompare(left, right);
    if (result != 0)
        return result;
    return QString::compare(left, right, Qt::CaseInsensitive);
}

int compareNaturalText(const QString &left, const QString &right)
{
    int leftPosition = 0;
    int rightPosition = 0;

    while (leftPosition < left.size() && rightPosition < right.size()) {
        const bool leftIsDigit = left.at(leftPosition).isDigit();
        const bool rightIsDigit = right.at(rightPosition).isDigit();

        if (leftIsDigit && rightIsDigit) {
            const int leftNumberStart = leftPosition;
            const int rightNumberStart = rightPosition;

            while (leftPosition < left.size() && left.at(leftPosition) == QLatin1Char('0'))
                ++leftPosition;
            while (rightPosition < right.size() && right.at(rightPosition) == QLatin1Char('0'))
                ++rightPosition;

            const int leftSignificantStart = leftPosition;
            const int rightSignificantStart = rightPosition;

            while (leftPosition < left.size() && left.at(leftPosition).isDigit())
                ++leftPosition;
            while (rightPosition < right.size() && right.at(rightPosition).isDigit())
                ++rightPosition;

            int leftSignificantLength = leftPosition - leftSignificantStart;
            int rightSignificantLength = rightPosition - rightSignificantStart;
            if (leftSignificantLength == 0)
                leftSignificantLength = 1;
            if (rightSignificantLength == 0)
                rightSignificantLength = 1;

            if (leftSignificantLength != rightSignificantLength)
                return leftSignificantLength < rightSignificantLength ? -1 : 1;

            const QString leftNumber = left.mid(leftSignificantStart, leftSignificantLength);
            const QString rightNumber = right.mid(rightSignificantStart, rightSignificantLength);
            const int numberResult = QString::compare(leftNumber, rightNumber, Qt::CaseInsensitive);
            if (numberResult != 0)
                return numberResult;

            const int leftNumberLength = leftPosition - leftNumberStart;
            const int rightNumberLength = rightPosition - rightNumberStart;
            if (leftNumberLength != rightNumberLength)
                return leftNumberLength < rightNumberLength ? -1 : 1;

            continue;
        }

        const int leftTextStart = leftPosition;
        const int rightTextStart = rightPosition;

        while (leftPosition < left.size() && left.at(leftPosition).isDigit() == leftIsDigit)
            ++leftPosition;
        while (rightPosition < right.size() && right.at(rightPosition).isDigit() == rightIsDigit)
            ++rightPosition;

        const QString leftText = left.mid(leftTextStart, leftPosition - leftTextStart).toCaseFolded();
        const QString rightText = right.mid(rightTextStart, rightPosition - rightTextStart).toCaseFolded();
        const int textResult = QString::localeAwareCompare(leftText, rightText);
        if (textResult != 0)
            return textResult;
    }

    if (leftPosition < left.size())
        return 1;
    if (rightPosition < right.size())
        return -1;
    return compareText(left, right);
}

} // namespace

class FileTableModel : public QAbstractTableModel
{
public:
    enum Column {
        ColumnIndex,
        ColumnFileName,
        ColumnCreatedTime,
        ColumnTimeDifference,
        ColumnCount
    };

    struct DurationStatistics
    {
        bool hasData = false;
        qint64 minimum = 0;
        qint64 maximum = 0;
        int minimumStartSequence = 0;
        int minimumEndSequence = 0;
        int maximumStartSequence = 0;
        int maximumEndSequence = 0;
        double average = 0.0;
    };

    explicit FileTableModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent)
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : order.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= order.size())
            return QVariant();

        const FileRecord &record = files.at(order.at(index.row()));

        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case ColumnIndex:
                return index.row() + 1;
            case ColumnFileName:
                return record.fileName;
            case ColumnCreatedTime:
                return record.createdTime.isValid()
                    ? record.createdTime.toString(QStringLiteral("yyyy/MM/dd HH:mm:ss.zzz"))
                    : QStringLiteral("-");
            case ColumnTimeDifference:
                return formatDuration(timeDifferenceMilliseconds(index.row()), precisionIndex);
            default:
                return QVariant();
            }
        }

        if (role == Qt::TextAlignmentRole) {
            switch (index.column()) {
            case ColumnIndex:
            case ColumnTimeDifference:
                return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
            case ColumnCreatedTime:
                return static_cast<int>(Qt::AlignCenter);
            default:
                return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
            }
        }

        if (role == Qt::ToolTipRole)
            return QDir::toNativeSeparators(record.absolutePath);

        return QVariant();
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();

        if (orientation == Qt::Vertical)
            return section + 1;

        switch (section) {
        case ColumnIndex:
            return QStringLiteral("序號");
        case ColumnFileName:
            return QStringLiteral("檔案名稱");
        case ColumnCreatedTime:
            return QStringLiteral("建立時間");
        case ColumnTimeDifference:
            return QStringLiteral("時間差");
        default:
            return QVariant();
        }
    }

    void sort(int column, Qt::SortOrder sortOrder = Qt::AscendingOrder) override
    {
        if (column < 0 || column >= ColumnCount || column == ColumnTimeDifference || order.size() < 2)
            return;

        beginResetModel();
        std::stable_sort(order.begin(), order.end(), [this, column, sortOrder](int leftIndex, int rightIndex) {
            const int result = compareRows(leftIndex, rightIndex, column);
            if (result == 0)
                return false;
            return sortOrder == Qt::AscendingOrder ? result < 0 : result > 0;
        });
        endResetModel();
    }

    void setFiles(const QVector<FileRecord> &records)
    {
        beginResetModel();
        files = records;
        order.clear();
        order.reserve(files.size());
        for (int index = 0; index < files.size(); ++index)
            order.append(index);
        endResetModel();
    }

    void setPrecisionIndex(int index)
    {
        precisionIndex = qBound(0, index, 3);
        if (rowCount() > 0) {
            emit dataChanged(this->index(0, ColumnTimeDifference),
                             this->index(rowCount() - 1, ColumnTimeDifference));
        }
    }

    qint64 timeDifferenceMilliseconds(int row) const
    {
        if (row <= 0 || row >= order.size())
            return -1;

        const FileRecord &previous = files.at(order.at(row - 1));
        const FileRecord &current = files.at(order.at(row));
        return absoluteMillisecondsBetween(previous.createdTime, current.createdTime);
    }

    DurationStatistics statistics() const
    {
        DurationStatistics result;
        qint64 total = 0;
        int count = 0;

        for (int row = 1; row < order.size(); ++row) {
            const qint64 milliseconds = timeDifferenceMilliseconds(row);
            if (milliseconds < 0)
                continue;

            if (!result.hasData) {
                result.minimum = milliseconds;
                result.maximum = milliseconds;
                result.minimumStartSequence = row;
                result.minimumEndSequence = row + 1;
                result.maximumStartSequence = row;
                result.maximumEndSequence = row + 1;
                result.hasData = true;
            } else {
                if (milliseconds < result.minimum) {
                    result.minimum = milliseconds;
                    result.minimumStartSequence = row;
                    result.minimumEndSequence = row + 1;
                }

                if (milliseconds > result.maximum) {
                    result.maximum = milliseconds;
                    result.maximumStartSequence = row;
                    result.maximumEndSequence = row + 1;
                }
            }

            total += milliseconds;
            ++count;
        }

        if (count > 0)
            result.average = static_cast<double>(total) / static_cast<double>(count);

        return result;
    }

    static QString formatDuration(double milliseconds, int precisionIndex)
    {
        if (milliseconds < 0.0)
            return QStringLiteral("-");

        switch (qBound(0, precisionIndex, 3)) {
        case 0:
            return QStringLiteral("%1 毫秒").arg(QLocale().toString(qRound64(milliseconds)));
        case 1:
            return QStringLiteral("%1 秒").arg(formatDecimal(milliseconds / 1000.0));
        case 2:
            return QStringLiteral("%1 分鐘").arg(formatDecimal(milliseconds / 60000.0));
        case 3:
            return QStringLiteral("%1 小時").arg(formatDecimal(milliseconds / 3600000.0));
        default:
            return QStringLiteral("-");
        }
    }

private:
    int compareRows(int leftIndex, int rightIndex, int column) const
    {
        const FileRecord &left = files.at(leftIndex);
        const FileRecord &right = files.at(rightIndex);

        switch (column) {
        case ColumnIndex:
            return left.originalIndex - right.originalIndex;
        case ColumnFileName: {
            const int fileNameResult = compareNaturalText(left.fileName, right.fileName);
            if (fileNameResult != 0)
                return fileNameResult;
            return compareText(left.absolutePath, right.absolutePath);
        }
        case ColumnCreatedTime:
            if (left.createdTime < right.createdTime)
                return -1;
            if (left.createdTime > right.createdTime)
                return 1;
            return compareNaturalText(left.fileName, right.fileName);
        default:
            return 0;
        }
    }

    QVector<FileRecord> files;
    QVector<int> order;
    int precisionIndex = 1;
};

AnalyzeFileTime::AnalyzeFileTime(QWidget *parent)
    : QMainWindow(parent)
    , fileModel(new FileTableModel(this))
    , timeDifferenceChart(new QtCharts::QChart())
    , timeDifferenceSeries(new QtCharts::QLineSeries())
    , timeDifferenceAxisX(new QtCharts::QValueAxis())
    , timeDifferenceAxisY(new QtCharts::QValueAxis())
    , activeSortColumn(FileTableModel::ColumnCreatedTime)
    , activeSortOrder(Qt::AscendingOrder)
{
    ui.setupUi(this);

    ui.tableViewFiles->setModel(fileModel);
    ui.tableViewFiles->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.tableViewFiles->setSelectionMode(QAbstractItemView::SingleSelection);
    ui.tableViewFiles->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui.tableViewFiles->setSortingEnabled(true);
    ui.tableViewFiles->verticalHeader()->setVisible(false);
    ui.tableViewFiles->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui.tableViewFiles->horizontalHeader()->setSectionResizeMode(FileTableModel::ColumnFileName, QHeaderView::Stretch);
    ui.tableViewFiles->setColumnWidth(FileTableModel::ColumnIndex, 60);
    ui.tableViewFiles->setColumnWidth(FileTableModel::ColumnCreatedTime, 190);
    ui.tableViewFiles->setColumnWidth(FileTableModel::ColumnTimeDifference, 120);
    ui.tableViewFiles->horizontalHeader()->setSortIndicator(activeSortColumn, activeSortOrder);

    timeDifferenceSeries->setName(QStringLiteral("時間差"));
    timeDifferenceChart->addSeries(timeDifferenceSeries);
    timeDifferenceChart->addAxis(timeDifferenceAxisX, Qt::AlignBottom);
    timeDifferenceChart->addAxis(timeDifferenceAxisY, Qt::AlignLeft);
    timeDifferenceSeries->attachAxis(timeDifferenceAxisX);
    timeDifferenceSeries->attachAxis(timeDifferenceAxisY);
    timeDifferenceChart->legend()->hide();
    timeDifferenceChart->setMargins(QMargins(6, 6, 6, 6));
    ui.chartViewTimeDifference->setChart(timeDifferenceChart);
    ui.chartViewTimeDifference->setRenderHint(QPainter::Antialiasing);

    fileModel->setPrecisionIndex(ui.comboBoxTimePrecision->currentIndex());

    connect(ui.toolButtonBrowseFolder, &QToolButton::clicked, this, &AnalyzeFileTime::selectFolder);
    connect(ui.lineEditFolderPath, &QLineEdit::editingFinished, this, &AnalyzeFileTime::loadFolder);
    connect(ui.tableViewFiles->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, &AnalyzeFileTime::handleSortIndicatorChanged);
    connect(ui.comboBoxTimePrecision, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        fileModel->setPrecisionIndex(index);
        updateStatistics();
    });
    connect(fileModel, &QAbstractItemModel::modelReset, this, &AnalyzeFileTime::updateStatistics);

    updateStatistics();
}

AnalyzeFileTime::~AnalyzeFileTime()
{}

void AnalyzeFileTime::selectFolder()
{
    const QString currentPath = ui.lineEditFolderPath->text().trimmed();
    const QString initialPath = QDir(currentPath).exists() ? currentPath : QDir::homePath();
    const QString folderPath = QFileDialog::getExistingDirectory(this, QStringLiteral("選擇要分析的資料夾"), initialPath);

    if (folderPath.isEmpty())
        return;

    ui.lineEditFolderPath->setText(QDir::toNativeSeparators(folderPath));
    loadFolder();
}

void AnalyzeFileTime::loadFolder()
{
    const QString folderPath = QDir::cleanPath(ui.lineEditFolderPath->text().trimmed());
    if (folderPath.isEmpty()) {
        fileModel->setFiles(QVector<FileRecord>());
        statusBar()->showMessage(QStringLiteral("請選擇要分析的資料夾。"));
        return;
    }

    const QDir folder(folderPath);
    if (!folder.exists()) {
        fileModel->setFiles(QVector<FileRecord>());
        statusBar()->showMessage(QStringLiteral("資料夾不存在。"));
        return;
    }

    QVector<FileRecord> records;
    const QDir::Filters filters = QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;

    QDirIterator iterator(folder.absolutePath(), filters, QDirIterator::NoIteratorFlags);
    while (iterator.hasNext()) {
        const QString absolutePath = iterator.next();
        const QFileInfo fileInfo(absolutePath);
        if (!fileInfo.isFile())
            continue;

        FileRecord record;
        record.originalIndex = records.size();
        record.fileName = fileInfo.fileName();
        record.absolutePath = fileInfo.absoluteFilePath();
        record.createdTime = fileCreatedTime(fileInfo);
        records.append(record);
    }

    fileModel->setFiles(records);
    sortFiles(activeSortColumn, activeSortOrder);
    statusBar()->showMessage(QStringLiteral("已載入 %1 個檔案。").arg(QLocale().toString(records.size())));
}

void AnalyzeFileTime::sortFiles(int column, Qt::SortOrder sortOrder)
{
    if (column == FileTableModel::ColumnTimeDifference)
        return;

    activeSortColumn = column;
    activeSortOrder = sortOrder;
    ui.tableViewFiles->sortByColumn(column, sortOrder);
}

void AnalyzeFileTime::handleSortIndicatorChanged(int column, Qt::SortOrder sortOrder)
{
    if (column == FileTableModel::ColumnTimeDifference) {
        const QSignalBlocker blocker(ui.tableViewFiles->horizontalHeader());
        ui.tableViewFiles->horizontalHeader()->setSortIndicator(activeSortColumn, activeSortOrder);
        return;
    }

    activeSortColumn = column;
    activeSortOrder = sortOrder;
}

void AnalyzeFileTime::updateStatistics()
{
    const FileTableModel::DurationStatistics statistics = fileModel->statistics();
    if (!statistics.hasData) {
        ui.labelMaxDifference->setText(QStringLiteral("-"));
        ui.labelMinDifference->setText(QStringLiteral("-"));
        ui.labelAverageDifference->setText(QStringLiteral("-"));
        updateChart();
        return;
    }

    const int precisionIndex = ui.comboBoxTimePrecision->currentIndex();
    ui.labelMaxDifference->setText(QStringLiteral("%1【%2-%3】")
        .arg(FileTableModel::formatDuration(statistics.maximum, precisionIndex))
        .arg(statistics.maximumStartSequence)
        .arg(statistics.maximumEndSequence));
    ui.labelMinDifference->setText(QStringLiteral("%1【%2-%3】")
        .arg(FileTableModel::formatDuration(statistics.minimum, precisionIndex))
        .arg(statistics.minimumStartSequence)
        .arg(statistics.minimumEndSequence));
    ui.labelAverageDifference->setText(FileTableModel::formatDuration(statistics.average, precisionIndex));
    updateChart();
}

void AnalyzeFileTime::updateChart()
{
    timeDifferenceSeries->clear();

    const int rowCount = fileModel->rowCount();
    const int precisionIndex = ui.comboBoxTimePrecision->currentIndex();
    double maximumValue = 0.0;
    int pointCount = 0;

    for (int row = 1; row < rowCount; ++row) {
        const qint64 milliseconds = fileModel->timeDifferenceMilliseconds(row);
        if (milliseconds < 0)
            continue;

        const double value = durationValue(milliseconds, precisionIndex);
        timeDifferenceSeries->append(row + 1, value);
        maximumValue = qMax(maximumValue, value);
        ++pointCount;
    }

    timeDifferenceAxisX->setTitleText(QStringLiteral("序號"));
    timeDifferenceAxisX->setLabelFormat(QStringLiteral("%d"));
    const int maximumSequence = qMax(2, rowCount);
    timeDifferenceAxisX->setTickCount(maximumSequence);
    timeDifferenceAxisX->setLabelsAngle(rowCount > 20 ? -90 : 0);
    timeDifferenceAxisX->setRange(1.0, maximumSequence);

    timeDifferenceAxisY->setTitleText(QStringLiteral("時間差（%1）").arg(durationUnitName(precisionIndex)));
    timeDifferenceAxisY->setLabelFormat(precisionIndex == 0 ? QStringLiteral("%.0f") : QStringLiteral("%.3f"));
    timeDifferenceAxisY->setTickCount(6);
    timeDifferenceAxisY->setRange(0.0, maximumValue > 0.0 ? maximumValue * 1.1 : 1.0);

    timeDifferenceChart->setTitle(pointCount > 0
        ? QStringLiteral("時間差折線圖")
        : QStringLiteral("時間差折線圖"));
}


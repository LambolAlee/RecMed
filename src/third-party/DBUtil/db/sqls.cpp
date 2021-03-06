#include "sqls.h"
#include "util/Config.h"

#include <QIODevice>
#include <QtDebug>
#include <QFile>
#include <QXmlStreamReader>
#include <QDir>


static const QLatin1String SQL_ID("id");
static const QLatin1String SQL_INCLUDED_DEFINE_ID("defineId");
static const QLatin1String SQL_TAGNAME_SQL("sql");
static const QLatin1String SQL_TAGNAME_SQLS("sqls");
static const QLatin1String SQL_TAGNAME_DEFINE("define");
static const QLatin1String SQL_TAGNAME_INCLUDE("include");
static const QLatin1String SQL_NAMESPACE("namespace");


/*-----------------------------------------------------------------------------|
 |                         SqlsPrivate implementation                          |
 |----------------------------------------------------------------------------*/
class SqlsPrivate
{
public:
    SqlsPrivate(Sqls *context);
    static QString buildKey(const QString &sqlNamespace, const QString &id);

private:
    bool read(QIODevice *device);
    bool open(QFile *file);
    QString errString() const;
    void handleStartElement(QStringView qName, const QXmlStreamAttributes &attrs);
    void handleEndElement(QStringView qName);

    QXmlStreamReader xml;

    QHash<QString, QString> _defines;
    QString _sqlNamespace;
    QString _currentSqlId;
    QString _currentDefineId;
    QString _currentIncludedDefineId;
    QString _currentText;

    Sqls *_context;

};


SqlsPrivate::SqlsPrivate(Sqls *context) : _context(context)
{
    QStringList sqlFiles = Config::instance().getDatabaseSqlFiles();

    for (const QString &fileName: sqlFiles) {
        QString fn = Config::resolve(fileName);
        qDebug() << QString("Loading SQL file: %1").arg(fn);

        QFile file(fn);
        if (!open(&file)) {
            _context->_flag = false;
            return;
        } else if (!read(&file)) {
            qDebug() << errString();
            _context->_flag = false;
            return;
        }
        _defines.clear();
    }
    _context->_flag = true;
}

bool SqlsPrivate::read(QIODevice *device)
{
    xml.setDevice(device);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement())
            handleStartElement(xml.name(), xml.attributes());
        else if (xml.isEndElement())
            handleEndElement(xml.name());
        else {
            _currentText += xml.text();
        }
    }
    return !xml.error();
}

bool SqlsPrivate::open(QFile *file)
{
    if (!file->open(QFile::ReadOnly | QFile::Text)) {
        qDebug() << QObject::tr("SQL storage xml") << QObject::tr("Cannot read file %1:\n%2.")
                         .arg(QDir::toNativeSeparators(file->fileName()), file->errorString());
        return false;
    }
    return true;
}

QString SqlsPrivate::buildKey(const QString &sqlNamespace, const QString &id) {
    return sqlNamespace + "::" + id;
}

QString SqlsPrivate::errString() const
{
    return QObject::tr("Parse error at line %1, column %2, message: %3")
           .arg(xml.errorString())
           .arg(xml.lineNumber())
           .arg(xml.columnNumber());
}

void SqlsPrivate::handleStartElement(QStringView qName, const QXmlStreamAttributes &attrs)
{
    // 1. ?????? SQL ??? xml ???????????? namespace, sql id, include ??? defineId, include ??? id
    // 2. ????????? <sql> ??????????????? currentText
    // 3. ????????? <define> ??????????????? currentText
    if (SQL_TAGNAME_SQL == qName) {
        // ????????????????????? SQL ??????
        _currentSqlId = attrs.value(SQL_ID).toString();
        _currentText = "";
    } else if (SQL_TAGNAME_INCLUDE == qName) {
        _currentIncludedDefineId = attrs.value(SQL_INCLUDED_DEFINE_ID).toString();
    } else if (SQL_TAGNAME_DEFINE == qName) {
        _currentDefineId = attrs.value(SQL_ID).toString();
        _currentText = "";
    } else if (SQL_TAGNAME_SQLS == qName) {
        _sqlNamespace = attrs.value(SQL_NAMESPACE).toString();
    }
}

void SqlsPrivate::handleEndElement(QStringView qName)
{
    // 1. ????????? <sql> ?????????????????? sqls
    // 2. ????????? <include> ??????????????? defines ????????????????????? sql
    // 3. ????????? <define> ?????????????????? defines
    if (SQL_TAGNAME_SQL == qName) {
        _context->_sqls.insert(buildKey(_sqlNamespace, _currentSqlId), _currentText.simplified());
        _currentText = "";
    } else if (SQL_TAGNAME_INCLUDE == qName) {
        QString defKey = buildKey(_sqlNamespace, _currentIncludedDefineId);
        QString def    = _defines.value(defKey);

        if (!def.isEmpty()) {
            _currentText += def;
        } else {
            qDebug() << "Cannot find define: " << defKey;
        }
    } else if (SQL_TAGNAME_DEFINE == qName) {
        _defines.insert(buildKey(_sqlNamespace, _currentDefineId), _currentText.simplified());
    }
}


/*-----------------------------------------------------------------------------|
 |                             Sqls implementation                             |
 |----------------------------------------------------------------------------*/
Sqls::Sqls()
{
    SqlsPrivate(this);
}

Sqls::~Sqls()
{
}

QString Sqls::getSql(const QString &sqlNamespace, const QString &sqlId) {
    QString sql = _sqls.value(SqlsPrivate::buildKey(sqlNamespace, sqlId));

    if (sql.isEmpty()) {
        qDebug() << QString("Cannot find SQL for %1::%2").arg(sqlNamespace, sqlId);
    }

    return sql;
}

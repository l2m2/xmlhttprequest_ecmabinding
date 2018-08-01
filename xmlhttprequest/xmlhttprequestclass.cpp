#include "xmlhttprequestclass.h"
#include <QEventLoop>
#include <qurlquery.h>

Q_DECLARE_METATYPE(QByteArray*)

XMLHttpRequestClass::XMLHttpRequestClass(QScriptEngine* engine) :
    _engine(engine),
    _async(true),
    _url(),
    _method(""),
    _responseType(""),
    _request(),
    _reply(NULL),
    _sendData(NULL),
    _rawResponseData(),
    _responseData(""),
    _onTimeout(QScriptValue::NullValue),
    _onReadyStateChange(QScriptValue::NullValue),
    _readyState(XMLHttpRequestClass::UNSENT),
    _errorCode(QNetworkReply::NoError),
    _timeout(0),
    _timer(this),
    _numRedirects(0) {

    _timer.setSingleShot(true);
}

XMLHttpRequestClass::~XMLHttpRequestClass() {
    if (_reply) { delete _reply; }
    if (_sendData) { delete _sendData; }
}

QScriptValue XMLHttpRequestClass::constructor(QScriptContext* context, QScriptEngine* engine) {
    return engine->newQObject(new XMLHttpRequestClass(engine));
}

QScriptValue XMLHttpRequestClass::getStatus() const {
    if (_reply) {
        return QScriptValue(_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
    }
    return QScriptValue(0);
}

QString XMLHttpRequestClass::getStatusText() const {
    if (_reply) {
        return _reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    }
    return "";
}

void XMLHttpRequestClass::abort() {
    abortRequest();
}

void XMLHttpRequestClass::setRequestHeader(const QString& name, const QString& value) {
    _request.setRawHeader(QByteArray(name.toLatin1()), QByteArray(value.toLatin1()));
}

void XMLHttpRequestClass::requestMetaDataChanged() {
    QVariant redirect = _reply->attribute(QNetworkRequest::RedirectionTargetAttribute);

    // If this is a redirect, abort the current request and start a new one
    if (redirect.isValid() && _numRedirects < MAXIMUM_REDIRECTS) {
        _numRedirects++;
        abortRequest();

        QUrl newUrl = _url.resolved(redirect.toUrl().toString());
        _request.setUrl(newUrl);
        doSend();
    }
}

void XMLHttpRequestClass::requestDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (_readyState == OPENED && bytesReceived > 0) {
        setReadyState(HEADERS_RECEIVED);
        setReadyState(LOADING);
    }
}

QScriptValue XMLHttpRequestClass::getAllResponseHeaders() const {
    if (_reply) {
        QList<QNetworkReply::RawHeaderPair> headerList = _reply->rawHeaderPairs();
        QByteArray headers;
        for (int i = 0; i < headerList.size(); i++) {
            headers.append(headerList[i].first);
            headers.append(": ");
            headers.append(headerList[i].second);
            headers.append("\n");
        }
        return QString(headers.data());
    }
    return QScriptValue("");
}

QScriptValue XMLHttpRequestClass::getResponseHeader(const QString& name) const {
    if (_reply && _reply->hasRawHeader(name.toLatin1())) {
        return QScriptValue(QString(_reply->rawHeader(name.toLatin1())));
    }
    return QScriptValue::NullValue;
}

void XMLHttpRequestClass::setReadyState(ReadyState readyState) {
    if (readyState != _readyState) {
        _readyState = readyState;
        if (_onReadyStateChange.isFunction()) {
            _onReadyStateChange.call(QScriptValue::NullValue);
        }
    }
}

void XMLHttpRequestClass::open(const QString& method, const QString& url, bool async) {
    if (_readyState == UNSENT) {
        _method = method;
        _url.setUrl(url);
        _async = async;
        _request.setUrl(_url);
        setReadyState(OPENED);
    }
}

void XMLHttpRequestClass::send() {
    send(QScriptValue::NullValue);
}

void XMLHttpRequestClass::send(const QScriptValue& data) {
    if (_readyState == OPENED && !_reply) {
        if (!data.isNull()) {
            _sendData = new QBuffer(this);
            if (data.isObject()) {
                QByteArray ba = qscriptvalue_cast<QByteArray>(data);
                _sendData->setData(ba);
            } else {
                _sendData->setData(data.toString().toUtf8());
            }
        }

        doSend();

        if (!_async) {
            QEventLoop loop;
            connect(this, SIGNAL(requestComplete()), &loop, SLOT(quit()));
            loop.exec();
        }
    }
}

void XMLHttpRequestClass::doSend() {

    _reply = _manager.sendCustomRequest(_request, _method.toLatin1(), _sendData);
    connectToReply(_reply);

    if (_timeout > 0) {
        _timer.start(_timeout);
        connect(&_timer, SIGNAL(timeout()), this, SLOT(requestTimeout()));
    }
}

void XMLHttpRequestClass::requestTimeout() {
    if (_onTimeout.isFunction()) {
        _onTimeout.call(QScriptValue::NullValue);
    }
    abortRequest();
    _errorCode = QNetworkReply::TimeoutError;
    setReadyState(DONE);
    emit requestComplete();
}

void XMLHttpRequestClass::requestError(QNetworkReply::NetworkError code) {
}

void XMLHttpRequestClass::requestFinished() {
    disconnect(&_timer, SIGNAL(timeout()), this, SLOT(requestTimeout()));

    _errorCode = _reply->error();

    if (_errorCode == QNetworkReply::NoError) {
        _rawResponseData.append(_reply->readAll());

        if (_responseType == "json") {
            _responseData = _engine->evaluate("(" + QString(_rawResponseData.data()) + ")");
            if (_responseData.isError()) {
                _engine->clearExceptions();
                _responseData = QScriptValue::NullValue;
            }
        } else {
            _responseData = QScriptValue(QString(_rawResponseData.data()));
        }
    }

    setReadyState(DONE);
    emit requestComplete();
}

void XMLHttpRequestClass::abortRequest() {
    // Disconnect from signals we don't want to receive any longer.
    disconnect(&_timer, SIGNAL(timeout()), this, SLOT(requestTimeout()));
    if (_reply) {
        disconnectFromReply(_reply);
        _reply->abort();
        _reply->deleteLater();
        _reply = NULL;
    }
}

void XMLHttpRequestClass::connectToReply(QNetworkReply* reply) {
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(requestError(QNetworkReply::NetworkError)));
    connect(reply, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(requestDownloadProgress(qint64, qint64)));
    connect(reply, SIGNAL(metaDataChanged()), this, SLOT(requestMetaDataChanged()));
}

void XMLHttpRequestClass::disconnectFromReply(QNetworkReply* reply) {
    disconnect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    disconnect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(requestError(QNetworkReply::NetworkError)));
    disconnect(reply, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(requestDownloadProgress(qint64, qint64)));
    disconnect(reply, SIGNAL(metaDataChanged()), this, SLOT(requestMetaDataChanged()));
}

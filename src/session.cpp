#include "session.h"

#include "backend.h"
#include "systemtheme.h"

Session::Session(SystemTheme *theme, QObject *parent)
    : QAbstractListModel(parent), m_theme(theme) {
    // The first document is the one entitled to adopt a draft left behind by an
    // abnormal exit; anything opened afterwards starts blank.
    m_documents.append(createDocument(true));
    m_currentIndex = 0;
}

Backend *Session::createDocument(bool recoverOrphans) {
    auto *document = new Backend(this, recoverOrphans);

    // Each document tracks the desktop's appearance for itself, the way the
    // single backend used to.
    if (m_theme) {
        document->setDarkMode(m_theme->darkMode());
        document->setTextScale(m_theme->textScale());
        connect(m_theme, &SystemTheme::darkModeChanged, document, &Backend::setDarkMode);
        connect(m_theme, &SystemTheme::textScaleChanged, document, &Backend::setTextScale);
    }

    return document;
}

int Session::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_documents.size());
}

QVariant Session::data(const QModelIndex &index, int role) const {
    if (role != DocumentRole || index.row() < 0 || index.row() >= m_documents.size())
        return {};
    return QVariant::fromValue(m_documents.at(index.row()));
}

QHash<int, QByteArray> Session::roleNames() const {
    return {{DocumentRole, QByteArrayLiteral("document")}};
}

Backend *Session::current() const {
    return at(m_currentIndex);
}

Backend *Session::at(int index) const {
    if (index < 0 || index >= m_documents.size())
        return nullptr;
    return m_documents.at(index);
}

void Session::setCurrentIndex(int index) {
    if (index < 0 || index >= m_documents.size() || index == m_currentIndex)
        return;

    m_currentIndex = index;
    emit currentIndexChanged();
}

void Session::newDocument() {
    const int row = static_cast<int>(m_documents.size());
    beginInsertRows(QModelIndex(), row, row);
    m_documents.append(createDocument(false));
    endInsertRows();
    emit documentsChanged();
    setCurrentIndex(row);
}

void Session::cycle() {
    if (m_documents.size() < 2)
        return;

    setCurrentIndex((m_currentIndex + 1) % m_documents.size());
}

bool Session::closeCurrent() {
    if (m_documents.size() < 2)
        return false;

    beginRemoveRows(QModelIndex(), m_currentIndex, m_currentIndex);
    Backend *closing = m_documents.takeAt(m_currentIndex);
    endRemoveRows();
    // Its recovery snapshot goes with it: the document is being dismissed on
    // purpose, so there is nothing to offer back on the next launch.
    closing->discardRecovery();
    closing->deleteLater();

    // Land on the neighbour that took its place, or the new last one.
    m_currentIndex = qBound(0, m_currentIndex, static_cast<int>(m_documents.size()) - 1);
    emit documentsChanged();
    emit currentIndexChanged();
    return true;
}

int Session::firstModified() const {
    for (int index = 0; index < m_documents.size(); ++index) {
        if (m_documents.at(index)->modified())
            return index;
    }
    return -1;
}

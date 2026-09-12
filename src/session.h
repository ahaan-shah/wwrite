#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QObject>

// Complete type: moc needs it for the Backend* property.
#include "backend.h"

class SystemTheme;

// The set of documents open in this process. Ctrl+N used to start a whole new
// process per window, which cost a fresh Qt runtime (~65 MB) each time and left
// an instance behind whenever one window outlived the others. Documents now
// live side by side in one process and the window shows one at a time.
class Session : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY documentsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(Backend *current READ current NOTIFY currentIndexChanged)

public:
    explicit Session(SystemTheme *theme, QObject *parent = nullptr);

    int count() const { return static_cast<int>(m_documents.size()); }
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    Backend *current() const;

    // A real list model, not a plain count: a Repeater driven by an int model
    // rebuilds every delegate when that int changes, which would throw away the
    // other documents' text areas -- and their text -- on each Ctrl+N.
    enum Roles { DocumentRole = Qt::UserRole + 1 };
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE Backend *at(int index) const;

    // Adds a blank document and makes it current.
    Q_INVOKABLE void newDocument();

    // Moves to the next document, wrapping at the end.
    Q_INVOKABLE void cycle();

    // Drops the current document. Returns false when it was the last one, which
    // the window takes as a request to close instead.
    Q_INVOKABLE bool closeCurrent();

    // Index of the first document with unsaved changes, or -1 when none have.
    Q_INVOKABLE int firstModified() const;

signals:
    void documentsChanged();
    void currentIndexChanged();

private:
    Backend *createDocument(bool recoverOrphans);

    QList<Backend *> m_documents;
    int m_currentIndex = -1;
    SystemTheme *m_theme = nullptr;
};

#pragma once

#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>

#include "track/trackid.h"

class GenreDao : public QObject {
    Q_OBJECT

  public:
    explicit GenreDao(const QSqlDatabase& database = QSqlDatabase());
    ~GenreDao() override = default;
    void initialize(const QSqlDatabase& database);

    // Genre management
    QStringList getAllGenres() const;
    int getOrCreateGenreId(const QString& genreName);
    bool deleteGenre(int genreId);

    // Track-genre relationships
    QStringList getTrackGenres(TrackId trackId) const;
    bool setTrackGenres(TrackId trackId, const QStringList& genres);
    bool addGenreToTrack(TrackId trackId, const QString& genre);
    bool removeGenreFromTrack(TrackId trackId, const QString& genre);

    // Utility methods
    void cleanupUnusedGenres();

  signals:
    void genreAdded(int genreId);
    void genreDeleted(int genreId);
    void trackGenresChanged(TrackId trackId);

  private:
    QSqlDatabase m_database;

    // Helper methods
    int getGenreIdByName(const QString& genreName) const;
    bool createGenre(const QString& genreName);

    Q_DISABLE_COPY(GenreDao)
};

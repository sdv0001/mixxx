#include "library/dao/genredao.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "moc_genredao.cpp"
#include "util/assert.h"
#include "util/logger.h"

namespace {
const mixxx::Logger kLogger("GenreDao");
} // anonymous namespace

GenreDao::GenreDao(const QSqlDatabase& database)
        : m_database(database) {
    VERIFY_OR_DEBUG_ASSERT(m_database.isOpen()) {
        kLogger.warning() << "GenreDao: Database is not open";
    }
}

void GenreDao::initialize(const QSqlDatabase& database) {
    VERIFY_OR_DEBUG_ASSERT(database.isOpen()) {
        kLogger.warning() << "GenreDao: Cannot initialize with closed database";
        return;
    }
    m_database = database;
}

QStringList GenreDao::getAllGenres() const {
    QStringList genres;
    QSqlQuery query(m_database);

    if (!query.exec("SELECT name FROM genres ORDER BY name")) {
        kLogger.warning() << "Failed to get all genres:" << query.lastError();
        return genres;
    }

    while (query.next()) {
        genres << query.value(0).toString();
    }

    return genres;
}

int GenreDao::getGenreIdByName(const QString& genreName) const {
    QSqlQuery query(m_database);
    query.prepare("SELECT id FROM genres WHERE name = ?");
    query.bindValue(0, genreName);

    if (!query.exec()) {
        kLogger.warning() << "Failed to get genre ID for" << genreName << ":" << query.lastError();
        return -1;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }

    return -1;
}

bool GenreDao::createGenre(const QString& genreName) {
    QSqlQuery query(m_database);
    query.prepare("INSERT INTO genres (name) VALUES (?)");
    query.bindValue(0, genreName);

    if (!query.exec()) {
        kLogger.warning() << "Failed to create genre" << genreName << ":" << query.lastError();
        return false;
    }

    int genreId = query.lastInsertId().toInt();
    emit genreAdded(genreId);
    return true;
}

int GenreDao::getOrCreateGenreId(const QString& genreName) {
    VERIFY_OR_DEBUG_ASSERT(!genreName.isEmpty()) {
        kLogger.warning() << "Cannot create genre with empty name";
        return -1;
    }

    QString trimmedName = genreName.trimmed();
    int existingId = getGenreIdByName(trimmedName);

    if (existingId != -1) {
        return existingId;
    }

    if (createGenre(trimmedName)) {
        return getGenreIdByName(trimmedName);
    }

    return -1;
}

QStringList GenreDao::getTrackGenres(TrackId trackId) const {
    QStringList genres;
    VERIFY_OR_DEBUG_ASSERT(trackId.isValid()) {
        return genres;
    }

    QSqlQuery query(m_database);
    query.prepare(
            "SELECT g.name FROM genres g "
            "JOIN genre_tracks gt ON g.id = gt.genre_id "
            "WHERE gt.track_id = ? "
            "ORDER BY g.name");
    query.bindValue(0, trackId.toVariant());

    if (!query.exec()) {
        kLogger.warning() << "Failed to get genres for track" << trackId << ":"
                          << query.lastError();
        return genres;
    }

    while (query.next()) {
        genres << query.value(0).toString();
    }

    return genres;
}

bool GenreDao::setTrackGenres(TrackId trackId, const QStringList& genres) {
    VERIFY_OR_DEBUG_ASSERT(trackId.isValid()) {
        return false;
    }

    // Start transaction
    if (!m_database.transaction()) {
        kLogger.warning() << "Failed to start transaction for setting track genres";
        return false;
    }

    // Remove existing genre associations
    QSqlQuery deleteQuery(m_database);
    deleteQuery.prepare("DELETE FROM genre_tracks WHERE track_id = ?");
    deleteQuery.bindValue(0, trackId.toVariant());

    if (!deleteQuery.exec()) {
        kLogger.warning()
                << "Failed to delete existing genre associations for track"
                << trackId << ":" << deleteQuery.lastError();
        m_database.rollback();
        return false;
    }

    // Add new genre associations
    for (const QString& genre : genres) {
        QString trimmedGenre = genre.trimmed();
        if (trimmedGenre.isEmpty()) {
            continue;
        }

        int genreId = getOrCreateGenreId(trimmedGenre);
        if (genreId == -1) {
            kLogger.warning() << "Failed to get/create genre" << trimmedGenre;
            m_database.rollback();
            return false;
        }

        QSqlQuery insertQuery(m_database);
        insertQuery.prepare("INSERT INTO genre_tracks (track_id, genre_id) VALUES (?, ?)");
        insertQuery.bindValue(0, trackId.toVariant());
        insertQuery.bindValue(1, genreId);

        if (!insertQuery.exec()) {
            kLogger.warning() << "Failed to insert genre association for track" << trackId
                              << "genre" << trimmedGenre << ":" << insertQuery.lastError();
            m_database.rollback();
            return false;
        }
    }

    // Commit transaction
    if (!m_database.commit()) {
        kLogger.warning() << "Failed to commit genre associations for track" << trackId;
        return false;
    }

    emit trackGenresChanged(trackId);
    return true;
}

bool GenreDao::addGenreToTrack(TrackId trackId, const QString& genre) {
    QStringList currentGenres = getTrackGenres(trackId);
    QString trimmedGenre = genre.trimmed();

    if (!trimmedGenre.isEmpty() && !currentGenres.contains(trimmedGenre)) {
        currentGenres << trimmedGenre;
        return setTrackGenres(trackId, currentGenres);
    }

    return true; // Genre already exists or is empty
}

bool GenreDao::removeGenreFromTrack(TrackId trackId, const QString& genre) {
    QStringList currentGenres = getTrackGenres(trackId);
    QString trimmedGenre = genre.trimmed();

    if (currentGenres.removeOne(trimmedGenre)) {
        return setTrackGenres(trackId, currentGenres);
    }

    return true; // Genre wasn't associated with track
}

bool GenreDao::deleteGenre(int genreId) {
    VERIFY_OR_DEBUG_ASSERT(genreId > 0) {
        return false;
    }

    // Start transaction
    if (!m_database.transaction()) {
        kLogger.warning() << "Failed to start transaction for deleting genre";
        return false;
    }

    // Delete genre associations first (due to foreign key constraint)
    QSqlQuery deleteAssocQuery(m_database);
    deleteAssocQuery.prepare("DELETE FROM genre_tracks WHERE genre_id = ?");
    deleteAssocQuery.bindValue(0, genreId);

    if (!deleteAssocQuery.exec()) {
        kLogger.warning() << "Failed to delete genre associations for genre"
                          << genreId << ":" << deleteAssocQuery.lastError();
        m_database.rollback();
        return false;
    }

    // Delete genre
    QSqlQuery deleteGenreQuery(m_database);
    deleteGenreQuery.prepare("DELETE FROM genres WHERE id = ?");
    deleteGenreQuery.bindValue(0, genreId);

    if (!deleteGenreQuery.exec()) {
        kLogger.warning() << "Failed to delete genre" << genreId << ":"
                          << deleteGenreQuery.lastError();
        m_database.rollback();
        return false;
    }

    // Commit transaction
    if (!m_database.commit()) {
        kLogger.warning() << "Failed to commit genre deletion for genre" << genreId;
        return false;
    }

    emit genreDeleted(genreId);
    return true;
}

void GenreDao::cleanupUnusedGenres() {
    QSqlQuery query(m_database);
    if (!query.exec("DELETE FROM genres WHERE id NOT IN (SELECT DISTINCT "
                    "genre_id FROM genre_tracks)")) {
        kLogger.warning() << "Failed to cleanup unused genres:" << query.lastError();
    }
}

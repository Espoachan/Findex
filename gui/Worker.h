#ifndef WORKER_H
#define WORKER_H

#include <QObject>
#include <QDebug>
#include "indexer.hpp"
#include "appdata.hpp"

class Worker : public QObject {
    Q_OBJECT

public:
    explicit Worker(USNIndexer* indexer, QObject *parent = nullptr) : QObject(parent), indexer(indexer) {}

public slots:
    void process() {
        if (!indexer) return;

        getAppDataPath();
        indexer->initVolume('C');
        
        UsnJournalInfo old_info{};
        bool has_old = indexer->loadJournalInfo(old_info);

        indexer->createUSNJournal();

        USN_JOURNAL_DATA current_data{};
        if(!indexer->getJournalData(current_data)) {
            emit finished();
            return;
        }

        bool fullIndex = false;
        if (!has_old) {
            fullIndex = true;
        } else if (old_info.journal_id != current_data.UsnJournalID) {
            fullIndex = true;
        } else if (old_info.next_usn < current_data.FirstUsn) {
            fullIndex = true;
        }

        if (fullIndex) {
            indexer->indexFiles();
            
            indexer->journal_info.journal_id = current_data.UsnJournalID;
            indexer->saveJournalInfo(indexer->journal_info);

            //indexer->incrementalIndex(indexer->journal_info.next_usn);
            fullIndex = false;
        } else {
            indexer->incrementalIndex(old_info.next_usn);
        }

        indexer->journal_info.journal_id = current_data.UsnJournalID;
        indexer->journal_info.next_usn = current_data.NextUsn;
        indexer->saveJournalInfo(indexer->journal_info);
        
        emit finished();
    }

signals:
    void finished();

private:
    USNIndexer* indexer;
};

#endif // WORKER_H
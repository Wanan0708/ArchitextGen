/**
 * @file DataSetting.h
 * @brief DataSetting - Data Persistence Layer
 * @domain Data_Persistence
 * 
 * This class handles data storage, database operations,
 * file I/O, and configuration management.
 */
#ifndef DATASETTING_H
#define DATASETTING_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QMap>

{{BASE_CLASS_INCLUDE}}

/**
 * @class DataSetting
 * @brief Data persistence handler
 * 
 * This class is part of the Data_Persistence domain and provides
 * data storage and retrieval functionality.
 */
class DataSetting : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param parent Optional parent object
     */
    explicit DataSetting(QObject *parent = nullptr);
    
    /**
     * @brief Virtual destructor
     */
    virtual ~DataSetting();

    void setHostname(QString hostname);


signals:
    /**
     * @brief Signal emitted when data is saved
     * @param key The data key
     */
    void dataSaved(const QString &key);
    
    /**
     * @brief Signal emitted when data is loaded
     * @param key The data key
     * @param value The loaded value
     */
    void dataLoaded(const QString &key, const QVariant &value);
    
    /**
     * @brief Signal emitted on storage error
     * @param error Error message
     */
    void storageError(const QString &error);

public slots:
    /**
     * @brief Save data to storage
     * @param key Data key
     * @param value Data value
     * @return true if successful
     */
    bool saveData(const QString &key, const QVariant &value);
    
    /**
     * @brief Load data from storage
     * @param key Data key
     * @return The loaded value
     */
    QVariant loadData(const QString &key);
    
    /**
     * @brief Delete data from storage
     * @param key Data key
     * @return true if successful
     */
    bool deleteData(const QString &key);

private:
    QMap<QString, QVariant> m_dataStore;
    QString m_storagePath;
};

#endif // DATASETTING_H

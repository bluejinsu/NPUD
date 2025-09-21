#ifndef ORACLE_ACCESS_H
#define ORACLE_ACCESS_H

#include "IDBAccess.h"

#include <boost/function.hpp>
#include <boost/thread.hpp>

#include <string>
#include <vector>


namespace oracle {
    namespace occi{
        class Environment;
        class Connection;
    }
}

struct OracleAccessInfo {
    std::string ipaddress;
    std::string sid;
    std::string user;
    std::string password;
};

class OracleDBAccess : public IDBAccess {
    boost::mutex _mtx;

    oracle::occi::Environment* _pEnv;
    oracle::occi::Connection* _pConn;

    std::string _ipaddress;
    std::string _sid;
    std::string _user;
    std::string _password;

public:
    OracleDBAccess(std::string ipaddress, std::string sid, std::string user, std::string password);
    virtual ~OracleDBAccess();

    bool initialize();
    void disconnect();

    virtual bool prepareTable(std::string tableName, std::string tableDefinition);
    virtual bool prepareSequence(std::string sequenceName);

    virtual bool isConnected();
    virtual void executeQuery(const std::string& sql, DB_ROW_CALLBACK fnRowMapper);
    virtual bool executeUpdate(const std::string& sql);
};

#endif
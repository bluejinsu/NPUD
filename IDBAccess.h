#ifndef IDBACCESS_H
#define IDBACCESS_H

#include <boost/function.hpp>

#include <string>
#include <vector>

typedef boost::function<void(std::vector<std::string>&)> DB_ROW_CALLBACK;

class IDBAccess {
public:
    virtual bool initialize() = 0;
    virtual bool isConnected() = 0;
    virtual void executeQuery(const std::string& sql, DB_ROW_CALLBACK fnRowMapper) = 0;
    virtual bool executeUpdate(const std::string& sql) = 0;
};

#endif
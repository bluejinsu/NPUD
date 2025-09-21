#include "OracleDBAccess.h"

#include <occi.h>

#include <iostream>
#include <sstream>

using namespace oracle::occi;

OracleDBAccess::OracleDBAccess(std::string ipaddress, std::string sid, std::string user, std::string password)
	: _pEnv(nullptr), _pConn(nullptr), _ipaddress(ipaddress), _sid(sid), _user(user), _password(password)
{

}

OracleDBAccess::~OracleDBAccess() {

}

bool OracleDBAccess::initialize() {
	boost::lock_guard<boost::mutex> lock(_mtx);

	try {
		_pEnv = Environment::createEnvironment(Environment::DEFAULT);

		std::stringstream ssConnectionString;
		ssConnectionString << _ipaddress;
		ssConnectionString << "/";
        ssConnectionString << _sid;

        _pConn = _pEnv->createConnection(_user, _password, ssConnectionString.str());
		return true;
    }
	catch (std::exception& e)
	{
		std::cerr << "OracleDBAccess::initialize error - " << e.what() << std::endl;
	}
	return false;
}

void OracleDBAccess::disconnect() {
	boost::lock_guard<boost::mutex> lock(_mtx);

	try {
		if (_pEnv) {
			if (_pConn) {
				_pEnv->terminateConnection(_pConn);
				_pConn = nullptr;
			}

			Environment::terminateEnvironment(_pEnv);
			_pEnv = nullptr;
		}
	}
	catch (std::exception e) {
		std::cerr << "OracleDBAccess::disconnect error - " << e.what() << std::endl;
	}

	_pConn = nullptr;
	_pEnv = nullptr;
}


bool OracleDBAccess::prepareTable(const std::string tableName, std::string tableDefinition) {
	if (!isConnected()) {
		return false;
	}

	// ���̺� ���� ���� Ȯ�� �� ���̺� ���� PL/SQL ������ ���� ���ڿ�
	std::string plsql = "DECLARE\n"
		"    v_table_exists NUMBER;\n"
		"BEGIN\n"
		"    -- ���̺� ���� ���� Ȯ��\n"
		"    SELECT COUNT(*)\n"
		"    INTO v_table_exists\n"
		"    FROM all_tables\n"
		"    WHERE table_name = '" + tableName + "';\n"
		"\n"
		"    -- ���̺��� ���� ��� ����\n"
		"    IF v_table_exists = 0 THEN\n"
		"        EXECUTE IMMEDIATE 'CREATE TABLE " + tableName + " (" + tableDefinition + ")';\n"
		"    END IF;\n"
		"END;";

	return executeUpdate(plsql);
}

bool OracleDBAccess::prepareSequence(std::string sequenceName) {
	if (!isConnected()) {
		return false;
	}

	// ������ ���� ���� Ȯ�� �� ������ ���� PL/SQL ������ ���� ���ڿ�
	std::string plsql = "DECLARE\n"
		"    v_sequence_exists NUMBER;\n"
		"BEGIN\n"
		"    -- ������ ���� ���� Ȯ��\n"
		"    SELECT COUNT(*)\n"
		"    INTO v_sequence_exists\n"
		"    FROM all_sequences\n"
		"    WHERE sequence_name = '" + sequenceName + "';\n"
		"\n"
		"    -- �������� ���� ��� ����\n"
		"    IF v_sequence_exists = 0 THEN\n"
		"        EXECUTE IMMEDIATE 'CREATE SEQUENCE " + sequenceName + "\n"
		"                           START WITH 1\n"
		"                           INCREMENT BY 1\n"
		"                           NOCACHE';\n"
		"    END IF;\n"
		"END;";

	return executeUpdate(plsql);
}

bool OracleDBAccess::isConnected() {
	return _pConn != nullptr;
}

void OracleDBAccess::executeQuery(const std::string& sql, DB_ROW_CALLBACK fnRowMapper) {
	boost::lock_guard<boost::mutex> lock(_mtx);

	if (!_pConn)
		return;

	Statement* pStatement = nullptr;

	std::vector<std::string> vecResults;
	try {
		pStatement = _pConn->createStatement();

		ResultSet* pResultSet = pStatement->executeQuery(sql);

		// Į�� ���� ������ �´�.
		std::vector<MetaData> columnList = pResultSet->getColumnListMetaData();
		int cols = columnList.size();

		while (pResultSet->next()) {
			std::vector<std::string> values;
			for (int c = 1; c <= cols; c++) {
				const std::string& value = pResultSet->getString(c);

				values.push_back(value);
			}

			fnRowMapper(values);
		}

		pStatement->closeResultSet(pResultSet);
	}
	catch (const SQLException& e) {
		int ec = e.getErrorCode();
		std::cerr << "OracleDBAccess::executeQuery error - " << ec << " - " << e.what() << " : " << sql.c_str() << std::endl;
	}
	catch (const std::exception& e) {
		std::cerr << "OracleDBAccess::executeQuery error - " << e.what() << std::endl;
	}

	try {
		if (pStatement) {
			_pConn->terminateStatement(pStatement);
		}
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
	}

	return;
}


/**
* @brief Insert, Update, Delete SQL���� �����ϰ� Ŀ���Ѵ�.
* @return ���� ���� �� ������ ���� ������� ��ȯ. ���� ���� �� -1�� ��ȯ
*/
bool OracleDBAccess::executeUpdate(const std::string& sql) {
	boost::lock_guard<boost::mutex> lock(_mtx);

	if (!_pConn)
		return false;

	bool success = true;
	Statement* pStatement = nullptr;
	try {
		pStatement = _pConn->createStatement();
		pStatement->setSQL(sql);

		pStatement->executeUpdate();

		_pConn->commit();
	}
	catch (const SQLException& e) {
		int ec = e.getErrorCode();
		std::cerr << "OracleDBAccess::executeUpdate error - " << ec << " - " << e.what() << " : " << sql.c_str() << std::endl;
		success = false;
		// disconnect();
	}
	catch (const std::exception& e) {
		std::cerr << "OracleDBAccess::executeUpdate error - " << e.what() << std::endl;
		success = false;
		// disconnect();
	}

	try {
		if (pStatement) {
			_pConn->terminateStatement(pStatement);
		}
	}
	catch (const SQLException& e) {
		std::cerr << "OracleDBAccess::executeUpdate error - " << e.what() << std::endl;
		return false;
	}
	catch (const std::exception& e) {
		std::cerr << "OracleDBAccess::executeUpdate error - " << e.what() << std::endl;
		// disconnect();
		return false;
	}

	return success;
}

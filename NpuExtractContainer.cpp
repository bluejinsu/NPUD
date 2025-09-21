#include "NpuExtractContainer.h"

#include "DataStorageSearcher.h"
#include "IDBAccess.h"
#include "NpuConfigure.h"
#include "NpuExtractJob.h"
#include "NpuWatchJob.h"
#include "OracleDBAccess.h"
#include "util.hpp"

#include <boost/thread.hpp>

#include <future>
#include <sstream>

void NpuExtractContainer::cleanupFutures() {
	_futures.erase(
		std::remove_if(_futures.begin(), _futures.end(),
			[](std::future<void>& f) {
				return !f.valid(); 
			}),
		_futures.end());
}

NpuExtractContainer::NpuExtractContainer(NpuConfigure* config)
	: _config(config)
{
}

NpuExtractContainer::~NpuExtractContainer() {

}

std::vector<NpuExtractInfo> NpuExtractContainer::getExtractList(time_t starttime, time_t endtime) {
	std::string db_ipaddress = _config->getValue("DB.IPADDRESS");
	std::string db_sid = _config->getValue("DB.SID");
	std::string db_user = _config->getValue("DB.USER");
	std::string db_password = _config->getValue("DB.PASSWORD");

	OracleDBAccess db_access(db_ipaddress
		, db_sid
		, db_user
		, db_password);

	if (!db_access.initialize()) {
		std::cerr << "Oracle::DBAccess::initailize returns false" << std::endl;
		return std::vector<NpuExtractInfo>();;
	}

	std::map<std::string, NpuExtractInfo> ext_info_map;

	std::stringstream sql;
	sql << "SELECT B.COLLECTMISSIONID, B.COLLECTFREQUENCY, B.COLLECTBANDWIDTH, " << std::endl;
	sql << " TO_CHAR(A.STARTTIME, 'YYYY-MM-DD HH24:MI:SS') AS STARTTIME," << std::endl;
	sql << " TO_CHAR(A.ENDTIME, 'YYYY-MM-DD HH24:MI:SS') AS ENDTIME, A.FILEPATH FROM COLLECTMODERESULTS A" << std::endl;
	sql << "LEFT JOIN COLLECTMISSION B ON A.COLLECTMISSIONID = B.COLLECTMISSIONID" << std::endl;
	sql << "WHERE A.STARTTIME BETWEEN TO_DATE('" << timestampToString(starttime) << "', 'YYYY-MM-DD HH24:MI:SS')" << std::endl;
	sql << "AND TO_DATE('" << timestampToString(endtime) << "', 'YYYY-MM-DD HH24:MI:SS')";

	db_access.executeQuery(sql.str(),
		[&ext_info_map](std::vector<std::string>& record)
		{
			if (record.size() < 6)
				return;

			std::string guid = record[0];
			int64_t frequency = atoll(record[1].c_str());
            int bandwidth = atoi(record[2].c_str());
			time_t starttime = localToUtcTimestamp(record[3]);
			time_t endtime = localToUtcTimestamp(record[4]);
			std::string filepath = record[5];
			int progress = 100;

			if (ext_info_map.find(guid) == ext_info_map.end()) {
				NpuExtractInfo info = {
					guid,
					frequency,
					bandwidth,
					starttime,
					endtime,
					progress,
					{ filepath }
				};
				ext_info_map[guid] = info;
			} else {
				ext_info_map[guid].files.push_back(filepath);
			}
		}
	);

	db_access.disconnect();

	// ExtractJob
	{
		boost::lock_guard<boost::mutex> lock(_mtx);
        for (auto it = _ext_job_container.begin(); it != _ext_job_container.end(); it++) {
			std::string guid = it->first;
			auto& ext_info = it->second->getExtractInfo();

			if (ext_info_map.find(guid) == ext_info_map.end())
				ext_info_map[guid] = ext_info;
		}
	}

	std::vector<NpuExtractInfo> ext_info_list;
	for (auto it = ext_info_map.begin(); it != ext_info_map.end(); it++) {
		ext_info_list.push_back(it->second);
	}

	return ext_info_list;
}

std::string NpuExtractContainer::createExtractJob(const NpuExtractRequest& ext_req) {
	std::string nsud_ipaddress = _config->getValue("NSUD.IPADDRESS");
    DataStorageSearcher searcher(nsud_ipaddress);
    auto data_storage_info = searcher.search(ext_req.starttime, ext_req.endtime, ext_req.frequency);

    if (!data_storage_info) {
        return nullptr;
    }

	auto job = std::make_shared<NpuExtractJob>(ext_req, _config, std::move(data_storage_info));
	std::string guid = job->start(
		[this](const std::string& guid) {
			auto f = std::async(std::launch::async, 
				[this, guid]() {
					boost::lock_guard<boost::mutex> lock(_mtx);
                    if (_ext_job_container.find(guid) != _ext_job_container.end()) {
                        _ext_job_container[guid]->wait();
                        _ext_job_container.erase(guid);
                    }
				});
			cleanupFutures();
			_futures.push_back(std::move(f));
		}
	);

	// lock
	{
		std::lock_guard<boost::mutex> lock(_mtx);
        _ext_job_container[guid] = job;
	}

	return guid;
}

bool NpuExtractContainer::deleteExtract(std::string guid) {
	std::string db_ipaddress = _config->getValue("DB.IPADDRESS");
	std::string db_sid = _config->getValue("DB.SID");
	std::string db_user = _config->getValue("DB.USER");
	std::string db_password = _config->getValue("DB.PASSWORD");

	OracleDBAccess db_access(db_ipaddress
		, db_sid
		, db_user
		, db_password);

	if (!db_access.initialize()) {
		std::cerr << "Oracle::DBAccess::initailize returns false" << std::endl;
		return false;
	}

    if (_ext_job_container.find(guid) != _ext_job_container.end()) {
        _ext_job_container[guid]->stop();
	}
	
	// Collectmission
	{
		std::stringstream sql;
		sql << "DELETE FROM COLLECTMISSION WHERE COLLECTMISSIONID = '" << guid << "'" << std::endl;
		db_access.executeUpdate(sql.str());
	}
	
	// Collectmoderesults
	{
		std::stringstream sql;
		sql << "DELETE FROM COLLECTMODERESULTS WHERE COLLECTMISSIONID = '" << guid << "'" << std::endl;
		db_access.executeUpdate(sql.str());
	}

	db_access.disconnect();
	// TODO : Remove associated files

	return true;
}

void NpuExtractContainer::updateExtract(std::string guid) {
	// TODO : Implement

}

std::vector<NpuWatchInfo> NpuExtractContainer::getWatchList() {
	std::vector<NpuWatchInfo> watch_info_list;

	// WatchJob
	{
		boost::lock_guard<boost::mutex> lock(_mtx);
		for (auto it = _watch_job_cotainer.begin(); it != _watch_job_cotainer.end(); it++) {
			watch_info_list.push_back(it->second->getWatchInfo());
		}
	}

	return watch_info_list;
}

bool NpuExtractContainer::getWatchInfo(const std::string guid, NpuWatchInfo& watch_info)
{
    boost::lock_guard<boost::mutex> lock(_mtx);
    if (_watch_job_cotainer.find(guid) != _watch_job_cotainer.end()) {
        auto job = _watch_job_cotainer[guid];
		watch_info = job->getWatchInfo();
        return true;
    }

    return false;
}

std::string NpuExtractContainer::createWatchJob(const NpuWatchRequest& watch_req) {
	std::string nsud_ipaddress = _config->getValue("NSUD.IPADDRESS");
	DataStorageSearcher searcher(nsud_ipaddress);
    auto data_storage_info = searcher.search(watch_req.starttime, watch_req.endtime, watch_req.frequency);

    if (!data_storage_info) {
        return nullptr;
    }

	auto job = std::make_shared<NpuWatchJob>(watch_req, _config, std::move(data_storage_info));
	std::string guid = job->start(
		[this](const std::string& guid) {
			auto f = std::async(std::launch::async,
				[this, guid]() {
					boost::lock_guard<boost::mutex> lock(_mtx);
                    if (_watch_job_cotainer.find(guid) != _watch_job_cotainer.end()) {
                        _watch_job_cotainer[guid]->wait();
                        _watch_job_cotainer.erase(guid);
					}
				});
			cleanupFutures();
			_futures.push_back(std::move(f));
		}
	);

    // lock
    {
        std::lock_guard<boost::mutex> lock(_mtx);
        _watch_job_cotainer[guid] = job;
    }

	return guid;
}

bool NpuExtractContainer::deleteWatchJob(std::string guid) {

	{
		std::lock_guard<boost::mutex> lock(_mtx);
		if (_watch_job_cotainer.find(guid) != _watch_job_cotainer.end()) {
			_watch_job_cotainer[guid]->stop();
            _watch_job_cotainer[guid]->wait();
			_watch_job_cotainer.erase(guid);
		}
	}
	return true;
}

void NpuExtractContainer::shutdown() {

	std::map<std::string, std::shared_ptr<NpuExtractJob>>::iterator it;
	while (true) {
		{
			boost::lock_guard<boost::mutex> lock(_mtx);
            it = _ext_job_container.begin();
            if (it == _ext_job_container.end())
				break;
		}

		it->second->stop();
	}

	for (auto& f : _futures) {
		if (f.valid()) f.wait();
	}
}

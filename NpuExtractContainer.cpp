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

#ifndef LOGE
#define LOGE(fmt, ...) do { /* ... */ } while(0)
#endif
#ifndef LOGI
#define LOGI(fmt, ...) do { /* ... */ } while(0)
#endif

void NpuExtractContainer::cleanupFutures() {
    using namespace std::chrono_literals;
    boost::lock_guard<boost::mutex> lock(_fut_mtx);

    auto it = _futures.begin();
    while (it != _futures.end()) {
        if (!it->valid()) {
            it = _futures.erase(it);
            continue;
        }
        // 논블로킹 상태 확인
        if (it->wait_for(0ms) == std::future_status::ready) {
            try {
                it->get(); // 예외 회수
            } catch (const std::exception& e) {
                LOGE("[cleanupFutures] future.get() exception: %s", e.what());
            } catch (...) {
                LOGE("[cleanupFutures] future.get() unknown exception");
            }
            it = _futures.erase(it);
        } else {
            ++it;
        }
    }
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
        return {};
    }

	auto job = std::make_shared<NpuExtractJob>(ext_req, _config, std::move(data_storage_info));
    std::string guid = job->getExtractInfo().guid;

    // lock
    {
        std::lock_guard<boost::mutex> lock(_mtx);
        _ext_job_container[guid] = job;
    }

    job->start(
		[this](const std::string& guid) {
			auto f = std::async(std::launch::async, [this, guid]() {
				std::shared_ptr<NpuExtractJob> ptr;

				{ // 1) job 포인터만 안전하게 꺼내오기
					boost::lock_guard<boost::mutex> lock(_mtx);
					auto it = _ext_job_container.find(guid);
					if (it != _ext_job_container.end()) ptr = it->second;
				}

				if (ptr) ptr->wait(); // 2) 락 없이 대기

				{ // 3) 완료 후 안전하게 erase
					boost::lock_guard<boost::mutex> lock(_mtx);
					_ext_job_container.erase(guid);
				}
			});

			cleanupFutures();           // 끝난 future 정리(내부에서 get() 호출 권장)
			_futures.push_back(std::move(f));
		}
	);

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
    
	
	// 진행 중 잡 정지
    {
        boost::lock_guard<boost::mutex> lock(_mtx);
        if (auto it = _ext_job_container.find(guid); it != _ext_job_container.end()) {
            it->second->stop();
        }
		// if (_ext_job_container.find(guid) != _ext_job_container.end()) {
        // 	_ext_job_container[guid]->stop();
		// }
    }
	// 락 없이 대기
    {
        std::shared_ptr<NpuExtractJob> ptr;
        {
            boost::lock_guard<boost::mutex> lock(_mtx);
            if (auto it = _ext_job_container.find(guid); it != _ext_job_container.end())
                ptr = it->second;
        }
        if (ptr) ptr->wait();
    }
	// 컨테이너에서 제거 (없으면 무시)
    {
        boost::lock_guard<boost::mutex> lock(_mtx);
        _ext_job_container.erase(guid);
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
    // idempotent: 중복 호출 방지
    bool expected = false;
    if (!_isShuttingDown.compare_exchange_strong(expected, true)) {
        // 이미 shutdown 중/완료
        return;
    }

    // 1) 현재 잡 스냅샷 생성 (락은 아주 짧게)
    std::vector<std::shared_ptr<NpuExtractJob>> jobs;
    {
        boost::lock_guard<boost::mutex> lock(_mtx);
        jobs.reserve(_ext_job_container.size());
        for (auto& kv : _ext_job_container)
            jobs.push_back(kv.second);
    }

    // 2) 모든 잡에 stop 신호 (락 밖)
    for (auto& j : jobs) {
        if (!j) continue;
        try {
            j->stop();
        } catch (const std::exception& e) {
            LOGE("[shutdown] job->stop() exception: %s", e.what());
        } catch (...) {
            LOGE("[shutdown] job->stop() unknown exception");
        }
    }

    // 3) 모든 잡 join/wait (락 밖)
    for (auto& j : jobs) {
        if (!j) continue;
        try {
            j->wait();
        } catch (const std::exception& e) {
            LOGE("[shutdown] job->wait() exception: %s", e.what());
        } catch (...) {
            LOGE("[shutdown] job->wait() unknown exception");
        }
    }

    // 4) 잡 컨테이너 비우기 (락 짧게)
    {
        boost::lock_guard<boost::mutex> lock(_mtx);
        _ext_job_container.clear();
    }

    // 5) futures 스냅샷+정리
    //  - 컨테이너 보호를 위해 복사해온 뒤, 멤버는 비워서 이후 push와 경쟁하지 않게
    std::vector<std::future<void>> futuresSnap;
    {
        boost::lock_guard<boost::mutex> lock(_fut_mtx);
        futuresSnap.swap(_futures); // _futures를 비움
    }

    // 6) 스냅샷된 future들에 대해 wait/get (락 밖)
    for (auto& f : futuresSnap) {
        if (!f.valid()) continue;
        try {
            f.wait();   // 블로킹 조인
            // 완료된 작업에서 예외 회수
            f.get();
        } catch (const std::exception& e) {
            LOGE("[shutdown] future.get() exception: %s", e.what());
        } catch (...) {
            LOGE("[shutdown] future.get() unknown exception");
        }
    }

    // 7) 안전 종료 플래그 해제(옵션: 다시 start 가능하게 하려면)
    _isShuttingDown.store(false);
    LOGI("[shutdown] done");
}

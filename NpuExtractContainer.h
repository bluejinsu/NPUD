#ifndef NPU_EXTRACT_CONTAINER_H
#define NPU_EXTRACT_CONTAINER_H

#include "NpuExtractInfo.h"
#include "NpuExtractRequest.h"
#include "NpuWatchInfo.h"
#include "NpuWatchRequest.h"

#include <boost/thread.hpp>

#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <time.h>

class NpuConfigure;
class NpuExtractJob;
class NpuWatchJob;
class IDBAccess;

class NpuExtractContainer {

public:
	NpuExtractContainer(NpuConfigure* config);
	~NpuExtractContainer();

	std::vector<NpuExtractInfo> getExtractList(time_t starttime, time_t endtime);
	std::string createExtractJob(const NpuExtractRequest& ext_req);
	bool deleteExtract(std::string guid);
	void updateExtract(std::string guid);

	std::vector<NpuWatchInfo> getWatchList();
    bool getWatchInfo(const std::string guid, NpuWatchInfo& watch_info);
	std::string createWatchJob(const NpuWatchRequest& watch_req);
	bool deleteWatchJob(std::string guid);

	void shutdown();

private:
    void cleanupFutures();

private:
    NpuConfigure* _config;

    boost::mutex _mtx;
	boost::mutex _fut_mtx;  // protects _futures
	std::atomic<bool> _isShuttingDown{false};  // 중복 shutdown 방지
    std::map<std::string, std::shared_ptr<NpuExtractJob>> _ext_job_container;
    std::map<std::string, std::shared_ptr<NpuWatchJob>> _watch_job_cotainer;
    std::vector<std::future<void>> _futures;
};

#endif

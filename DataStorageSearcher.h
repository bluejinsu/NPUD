#ifndef DATA_STORAGE_SEARCHER_H
#define DATA_STORAGE_SEARCHER_H

#include "DataStorageInfo.h"
#include "NpuExtractRequest.h"

#include <memory>
#include <string>

#include <inttypes.h>
#include <time.h>

class DataStorageSearcher {
private:
	std::string _ipaddress;

public:
	DataStorageSearcher(std::string ipaddress);
	~DataStorageSearcher();
	
	std::unique_ptr<DataStorageInfo> search(const time_t starttime, const time_t endttime, const int64_t frequency);
};

#endif
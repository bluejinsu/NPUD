#include "DataStorageSearcher.h"

#include <RestClient.h>

DataStorageSearcher::DataStorageSearcher(std::string ipaddress)
    : _ipaddress(ipaddress)
{

}

DataStorageSearcher::~DataStorageSearcher() {

}

std::unique_ptr<DataStorageInfo> DataStorageSearcher::search(const time_t starttime, const time_t endtime, const int64_t frequency) {
    RestClient rest_client;

    Json::Value json_response;
    std::string nsud_ipaddress = _ipaddress;
    std::string url = "http://" + nsud_ipaddress + "/nsu/storage-info-list";
    if (!rest_client.get(url, json_response))
        return nullptr;

    if (!json_response.isArray())
        return nullptr;

    for (int i = 0; i < json_response.size(); i++) {
        auto instance = json_response[i];
        int instance_id = instance["instance-id"].asInt();

        /*if (instance_id != ext_req.instance)
            continue;*/

        auto storage_info_list = instance["storage-info"];
        if (!storage_info_list.isArray())
            continue;

        for (int j = 0; j < storage_info_list.size(); j++) {
            auto storage_info = storage_info_list[j];

            time_t storage_starttime = storage_info["starttime"].asInt64();
            time_t storage_endtime = storage_info["endtime"].asInt64();

            int64_t storage_frequency = storage_info["frequency"].asInt64();
            int64_t samplerate = storage_info["samplerate"].asInt64();;
            int64_t start_freq = storage_frequency - samplerate / 2;
            int64_t end_freq = storage_frequency + samplerate / 2;

            if (storage_starttime <= starttime && endtime < storage_endtime) {
                if (start_freq <= frequency && frequency < end_freq) {
                    std::string directory = storage_info["directory"].asCString();
#ifdef _WIN32
                    int pos = directory.find("/data");
                    directory.replace(pos, 5, "Z:");
#endif
                    std::unique_ptr<DataStorageInfo> dat_storage_info(new DataStorageInfo);
                    dat_storage_info->wddc_freq = storage_frequency;
                    dat_storage_info->starttime = storage_starttime;
                    dat_storage_info->endtime = storage_endtime;
                    dat_storage_info->samplerate = samplerate;

                    dat_storage_info->frame_samples = 524288 / sizeof(int) / 2;
                    dat_storage_info->storage_dir = directory + "dat/";

                    return dat_storage_info;
                }
            }
        }
    }

    return nullptr;
}

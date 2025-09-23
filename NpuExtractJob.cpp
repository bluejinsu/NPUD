#include "NpuExtractJob.h"	// 250919 03:36

#include "DataStorageInfo.h"
#include "DataStorageSearcher.h"
#include "INpuExtractFileWriter.h"
#include "NpuBasicFileNameFormatter.h"
#include "NpuConfigure.h"
#include "NpuExtract16tWriter.h"
#include "NpuExtractRequest.h"
#include "NpuExtractWavWriter.h"
#include "OracleDBAccess.h"
#include "RestClient.h"
#include "util.hpp"

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <fstream>
#include <functional>
#include <memory>
#include <iostream>
#include <thread>      // [ADD] sleep_for
#include <chrono>      // [ADD] milliseconds
#include <boost/filesystem.hpp>   // ✅ 추가: boost::filesystem::path 멤버 사용

#undef min

const int HEADER_SIZE = 28;
const int BODY_SIZE = 524288;
const int FRAME_SIZE = HEADER_SIZE + BODY_SIZE;
const int FRAMES_PER_FILE = 256;

#pragma pack(push, 1)
struct TVHHeader {
    int stream_id;
    short stream_type;
    short data_type;
    int stream_addr;
    int stream_size;
};
#pragma pack(pop)

struct stTimeRange {
    time_t starttime;
    time_t endtime;
};

// -------------------- [ADD] 유틸: 퍼센트 클램프 --------------------
namespace {
    inline int clampPercent(int v) {
        return (v < 0) ? 0 : (v > 100 ? 100 : v);
    }
}
// -------------------------------------------------------------------

void NpuExtractJob::loadConfig() {
    _config_data.level_offset = atof(_config->getValue("DATA.LEVEL_OFFSET").c_str());
    _config_data.ant_volt_ref = atof(_config->getValue("DATA.ANT_VOLTAGE_REF").c_str());
    std::string output_path = _config->getValue("EXTRACT_OUTPUT_PATH");
    _config_data.output_path = output_path;
}

void NpuExtractJob::onInitialized() {
    _thumb_image = cv::Mat::zeros(100, 100, CV_8UC3);
}

void NpuExtractJob::onClosed() {
    cv::Mat img = cv::Mat::zeros(100, 100, CV_64FC1);

    int total_lines = _spec_lines.size();
    for (int k = 0; k < total_lines; k++) {
        std::vector<double>& line = _spec_lines[k];
        cv::Mat line_mat(1, line.size(), CV_64FC1, line.data());
        cv::vconcat(img, line_mat, img);
    }

    std::string filename = "/data/analysis/spec.png";
    //cv::imwrite(filename, img);
}

void NpuExtractJob::onReadFrame(int total_frame, int frame_num) {
    _ext_info.progress = (int)(100.0 * frame_num / (double)total_frame);
}

void NpuExtractJob::onFullBuffer(time_t timestamp, float* ddc_iq, size_t ddc_samples) {
    std::vector<double> iq_data_d;
    std::vector<double> fft_output;
    std::vector<double> amp_output;

    // ===== [CHANGE] 실제 복소 샘플 개수 N으로 안전 처리 =====
    const int N = static_cast<int>(ddc_samples);
    iq_data_d.resize(static_cast<size_t>(N) * 2);
    fft_output.resize(static_cast<size_t>(N) * 2);

    for (int k = 0; k < N; k++) {
        iq_data_d[2 * k]     = (double)ddc_iq[2 * k];
        iq_data_d[2 * k + 1] = (double)ddc_iq[2 * k + 1];
    }

    // ===== [CHANGE] FFT 길이를 N으로 맞춰 실행 (내부에서 plan 재생성 보장) =====
    _fft_executor.execute(iq_data_d.data(), fft_output.data(), N);
    // ===============================================================

    for (int i = 0; i < N; i++) {
        double real = fft_output[2 * i];
        double imag = fft_output[2 * i + 1];

        // ===== [CHANGE] 로그 안전장치 (0 보호) =====
        if (real == 0) real += 1e-7;
        if (imag == 0) imag += 1e-7;

        double power = 10 * log10(real * real + imag * imag) + _config_data.level_offset - 20 * log10((double)N);
        // ===========================================

        if (_max_channel_power < power)
            _max_channel_power = power;

        amp_output.push_back(power);
    }

    _spec_lines.push_back(amp_output);

    encoder->setAntennaVoltageRef(_config_data.ant_volt_ref);
    auto encoded = encoder->encodeIQData((const char*)ddc_iq, ddc_samples * sizeof(float) * 2);

    outDDCFile.write(encoded->data(), encoded->size());
    _file_writer->write(ddc_iq, ddc_samples);

    // -------------------- [ADD] 진행률 계산: timestamp(ms) 기반 --------------------
    const long long start_ms = static_cast<long long>(_ext_info.starttime) * 1000LL;
    const long long end_ms   = static_cast<long long>(_ext_info.endtime)   * 1000LL;
    const long long total_ms = std::max(1LL, end_ms - start_ms); // 0 나눗셈 방지
    const long long cur_ms   = std::max(0LL, std::min(end_ms - start_ms, static_cast<long long>(timestamp) - start_ms));

    int percent = static_cast<int>((100.0L * cur_ms) / total_ms);
    _ext_info.progress = clampPercent(percent);
    // -------------------------------------------------------------------------------
}

std::unique_ptr<NpuExtractResult> NpuExtractJob::extractIQ(const DataStorageInfo* dat_storage_info, INpuExtractFileWriter* file_writer, INpuFileNameFormatter* filename_formatter) {
    if (!_ddc_exe.initDDC(_ext_info.starttime, _ext_info.endtime, _ext_info.frequency, _ext_info.bandwidth, dat_storage_info))
        return nullptr;

    _file_writer = file_writer;

    unsigned int channel_samplerate = _ddc_exe.getChannelSamplerate();
    unsigned int channel_bandwidth = _ddc_exe.getChannelBandwidth();
    unsigned int ddc_samples = _ddc_exe.getDDCSamples();    
    

    output_path = boost::filesystem::path(_config_data.output_path + "audio/" + _ext_info.guid);        

    boost::system::error_code ec;
    if (!boost::filesystem::create_directory(output_path, ec)) {
        std::cerr << "Failed to create ddc result directory." << std::endl;
        return nullptr;
    }

    fileName = filename_formatter->getFileName(_ext_info.frequency, channel_samplerate, channel_bandwidth, _ext_info.starttime);
    filepath = output_path.string() + "/" + fileName + ".dat";

    encoder.reset(new IFDataFormatEncoder(channel_samplerate, _ext_info.frequency, channel_bandwidth, _ext_info.starttime));
    outDDCFile.open(filepath, std::ios::binary | std::ios::out);
    if (!outDDCFile) {
        std::cerr << "Failed to open ddc result file." << std::endl;
        return nullptr;
    }

    if(!file_writer->init(output_path.string(), _ext_info.frequency, channel_samplerate, channel_bandwidth, _ext_info.starttime)) {
        std::cerr << "Failed to init file writer." << std::endl;
        return nullptr;
    }

    _fft_executor.init(ddc_samples);
    _max_channel_power = -9999.9;

    // ====== [PATCH] 실행 루프 (ioDone+ringEmpty 재확인 N회) ======
    {
        constexpr int kMaxIdleZeroReadableChecks = 10;   // 재확인 횟수
        constexpr int kIdleSleepMs               = 2;    // 재확인 간격(ms)

        while (_ddc_exe.getRunning()) {
            bool didWork = _ddc_exe.executeDDC(this, 2);

            if (!didWork) {
                // ioDone + ringEmpty → 짧은 재확인 루프
                if (_ddc_exe.ioDone() && _ddc_exe.ringReadable() == 0) {
                    bool stillEmpty = true;
                    for (int retry = 0; retry < kMaxIdleZeroReadableChecks; ++retry) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(kIdleSleepMs));
                        if (_ddc_exe.ringReadable() > 0) {
                            stillEmpty = false;
                            break;  // 다시 읽을 게 있으니 루프 바깥으로 복귀
                        }
                    }
                    if (stillEmpty) {
                        break; // 끝까지 비어 있으면 종료
                    } else {
                        continue; // 다시 executeDDC 시도
                    }
                }

                // 그 외 상황은 짧게 쉼
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    _ddc_exe.close(); // stop + join

    outDDCFile.close();
    _file_writer->close();
    
    // -------------------- [ADD] 완료 보정 --------------------
    _ext_info.progress = 100;
    // --------------------------------------------------------

    auto ext_result = boost::make_unique<NpuExtractResult>();
    ext_result->guid = _ext_info.guid;
    ext_result->filepath = filepath;
    ext_result->starttime = _ext_info.starttime;
    ext_result->endtime = _ext_info.endtime;
    ext_result->frequency = _ext_info.frequency;
    ext_result->samplerate = channel_samplerate;
    ext_result->bandwidth = channel_bandwidth;
    ext_result->power_level = _max_channel_power;

    return ext_result;
} 

bool saveDatabase(const OracleAccessInfo* ora_access_info, const NpuExtractResult* ext_result) {
    OracleDBAccess db_access(ora_access_info->ipaddress
                            , ora_access_info->sid
                            , ora_access_info->user
                            , ora_access_info->password);

    if (!db_access.initialize()) {
        std::cerr << "Oracle::DBAccess::initailize returns false" << std::endl;
        return false;
    }

    std::string mission_id = ext_result->guid;
    int device_id = 1;
    int64_t fc = ext_result->frequency;
    int bw = ext_result->bandwidth;
    int fs = ext_result->samplerate;
    time_t starttime = ext_result->starttime;
    time_t endtime = ext_result->endtime;
    double power_level = ext_result->power_level;
    int demod_type = 99;
    double threshold = 0;
    int holdtime = 0;
    int continuetime = 0;
    int antport = 0;
    std::string comment = "";
    std::string filepath = convertPathToWindowsFormat(ext_result->filepath);

    std::string result_id = mission_id;

    // insert collectmission
    {
        std::stringstream sql;
        sql << "insert into collectmission values(";
        sql << mission_id;
        sql << ",";
        sql << device_id;
        sql << ",";
        sql << fc;
        sql << ",";
        sql << bw;
        sql << ",";
        sql << fs;
        sql << ",";
        sql << 0;
        sql << ",";
        sql << demod_type;
        sql << ",";
        sql << threshold;
        sql << ",";
        sql << "to_date('";
        sql << std::put_time(localtime(&starttime), "%Y.%m.%d %H:%M:%S");
        sql << "', 'YYYY.MM.DD HH24:MI:SS')";
        sql << ",";
        sql << holdtime;
        sql << ",";
        sql << continuetime;
        sql << ",";
        sql << antport;
        sql << ",";
        sql << "'" + comment + "'";
        sql << ")";

        if (!db_access.executeUpdate(sql.str())) {
            std::cerr << "Failed to insert collectmission" << std::endl;
            db_access.disconnect();
            return false;
        }
    }

    // insert collectresult
    {
        std::stringstream sql;
        sql << "insert into collectmoderesults values(";
        sql << result_id;
        sql << ",";
        sql << mission_id;
        sql << ",";
        sql << "to_date('";
        sql << std::put_time(localtime(&starttime), "%Y.%m.%d %H:%M:%S");
        sql << "', 'YYYY.MM.DD HH24:MI:SS')";
        sql << ",";
        sql << "to_date('";
        sql << std::put_time(localtime(&endtime), "%Y.%m.%d %H:%M:%S");
        sql << "', 'YYYY.MM.DD HH24:MI:SS')";
        sql << ",";
        sql << "'";
        sql << filepath;
        sql << "'";
        sql << ", ''"; // Comment
        sql << ", ";
        sql << power_level;
        sql << ")";

        if (!db_access.executeUpdate(sql.str())) {
            std::cerr << "Failed to insert collectmoderesults" << std::endl;
            db_access.disconnect();
            return false;
        }
    }

    db_access.disconnect();
    return true;
}


void NpuExtractJob::work() {
	while (true) {
		// lock
        {
            boost::lock_guard<boost::mutex> lock(_mtx);
            if (!_running)
                break;
        }

        if (_ext_info.starttime >= _ext_info.endtime) {
            std::cerr << "Invalid time range" << std::endl;
            break;
        }

        if (_ext_info.starttime < _dat_storage_info->starttime) {
            std::cerr << "Invalid starttime" << std::endl;
            break;
        }

        if (_ext_info.endtime > _dat_storage_info->endtime) {
            std::cerr << "Invalid endtime" << std::endl;
            break;
        }

        std::unique_ptr<INpuExtractFileWriter> file_writer;

        std::string sitename = _config->getValue("SITE_NAME");
        NpuBasicFileNameFormatter filename_formatter(sitename);

        if (_ext_req.filetype == "16t") {
            file_writer.reset(new NpuExtract16tWriter(&filename_formatter));
        } else if (_ext_req.filetype == "wav") {
            file_writer.reset(new NpuExtractWavWriter(&filename_formatter));
        }

        int64_t start_sample_pos = (_ext_info.starttime - _dat_storage_info->starttime) * _dat_storage_info->samplerate;
        int64_t end_sample_pos = (_ext_info.endtime - _dat_storage_info->starttime) * _dat_storage_info->samplerate;
        int64_t sample_count = end_sample_pos - start_sample_pos;
        double frame_count = sample_count / (double)_dat_storage_info->frame_samples;

        std::cout << "start_sample_pos : " << start_sample_pos << std::endl;
        std::cout << "end_sample_pos : " << end_sample_pos << std::endl;
        std::cout << "frame_samples : " << _dat_storage_info->frame_samples << std::endl;
        std::cout << "sample_count : " << sample_count << std::endl;
        std::cout << "frame_count : " << frame_count << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        auto ext_result = extractIQ(_dat_storage_info.get(), file_writer.get(), &filename_formatter);
        if (!ext_result) {
            break;
        }

        if (!_running)
            break;

        std::string db_ipaddress = _config->getValue("DB.IPADDRESS");
        std::string db_sid = _config->getValue("DB.SID");
        std::string db_user = _config->getValue("DB.USER");
        std::string db_password = _config->getValue("DB.PASSWORD");

        OracleAccessInfo oracle_info;
        oracle_info.ipaddress = db_ipaddress;
        oracle_info.user = db_user;
        oracle_info.password = db_password;
        oracle_info.sid = db_sid;

        saveDatabase(&oracle_info, ext_result.get());

        auto end_time = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> elapsed = end_time - start_time;

        std::cout << "Execution time: " << elapsed.count() << " sec" << std::endl;

        break;
	}

    if (_completed_callback)
        _completed_callback(_ext_info.guid);
}

NpuExtractJob::NpuExtractJob(const NpuExtractRequest& ext_req, NpuConfigure* config, std::unique_ptr<DataStorageInfo> data_storage_info)
	: _ext_req(ext_req)
    , _config(config)
    , _running(false)
    , _dat_storage_info(std::move(data_storage_info))
{
    _ext_info.frequency = ext_req.frequency;
    _ext_info.bandwidth = ext_req.bandwidth;
    _ext_info.starttime = ext_req.starttime;
    _ext_info.endtime = ext_req.endtime;
	_ext_info.progress = 0;
    _ext_info.guid = generateID(); // ← 여기로 이동
    // memset(&_config_data, 0, sizeof(_config_data));
}

NpuExtractJob::~NpuExtractJob() {
}

std::string NpuExtractJob::start(JOB_COMPLEDTED_CALLBACK completed_callback) {
	boost::lock_guard<boost::mutex> lock(_mtx);

	if (_running)
		return _ext_info.guid;

    loadConfig();

    //_ext_info.guid = generateID();
    _completed_callback = completed_callback;
	_running = true;
	_thread.reset(new std::thread(std::bind(&NpuExtractJob::work, shared_from_this())));

    return _ext_info.guid;
}

void NpuExtractJob::stop() {
    _ddc_exe.close();
}

void NpuExtractJob::wait() {
    if (_thread && _thread->joinable()) {
        _thread->join();
    }
}

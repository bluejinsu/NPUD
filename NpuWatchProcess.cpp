#include "NpuWatchProcess.h"
#include "NpuConfigure.h"
#include "IFDataFormatEncoder.h"
#include "NpuExtract16tWriter.h"
#include "NpuBasicFileNameFormatter.h"
#include "NpuExtractWavWriter.h"
#include "NpuWatchRequest.h"
#include "NpuWatchResult.h"
#include "OracleDBAccess.h"
#include "util.hpp"

#include <boost/filesystem.hpp>

#include <math.h>
#include <iostream>

NpuWatchProcess::NpuWatchProcess(NpuConfigure* config)
    : _config(config)
    , _avg_count(0)
    , _max_channel_power(-1000)
    , _state(EnWatchState::DEAD)
    , _filename_formatter(config->getValue("SITE_NAME"))
{
}

NpuWatchProcess::~NpuWatchProcess()
{
}

void NpuWatchProcess::loadConfig() {
    _config_data.level_offset = atof(_config->getValue("DATA.LEVEL_OFFSET").c_str());
    _config_data.ant_volt_ref = atof(_config->getValue("DATA.ANT_VOLTAGE_REF").c_str());
    std::string output_path = _config->getValue("EXTRACT_OUTPUT_PATH");
    _config_data.output_path = output_path;
}

void NpuWatchProcess::init(const NpuWatchRequest& watch_req, const NpuWatchInfo& watch_info, const size_t ddc_samples) {
    _watch_info = watch_info;
    _watch_req = watch_req;

    std::unique_ptr<INpuExtractFileWriter> file_writer;

    if (_watch_req.filetype == "16t") {
        file_writer.reset(new NpuExtract16tWriter(&_filename_formatter));
    } else if (_watch_req.filetype == "wav") {
        file_writer.reset(new NpuExtractWavWriter(&_filename_formatter));
    }

    // [CHANGE] 사용자가 선택한 라이터를 실제로 적용 (기존: 강제로 16t 생성)
    // _file_writer는 원래 raw pointer로 보이므로 release 사용
    if (file_writer) {
        _file_writer = file_writer.release();
    } else {
        // fallback: 기본 16t
        _file_writer = new NpuExtract16tWriter(&_filename_formatter);
    }

    // [CHANGE] FFT 초기 길이는 최초 ddc_samples로 준비하되,
    //           실제 실행 시 들어오는 길이에 맞춰 자동 재생성(아래 execute에서 length 전달)
    _fft_executor.init(static_cast<int>(ddc_samples));

    _watchFileSaveInfo._output_path = boost::filesystem::path(_config_data.output_path + _watch_info.guid);
    boost::filesystem::path spec_dir(_watchFileSaveInfo._output_path.string() + "/" + "spec");
    boost::system::error_code ec;
    if (!boost::filesystem::create_directory(_watchFileSaveInfo._output_path, ec)) {
        std::cerr << "Failed to create ddc result directory - " << ec.message() << std::endl;
    } else {
        boost::filesystem::create_directory(spec_dir, ec);
    }
}

double NpuWatchProcess::getPowerLevel(const float* ddc_iq, size_t ddc_samples) {
    double max_power_level = -999.0;

    std::vector<double> iq_data_d;
    std::vector<double> fft_output;

    // ddc_samples: "복소 샘플 개수"
    const int N = static_cast<int>(ddc_samples);

    iq_data_d.resize(static_cast<size_t>(N) * 2);
    fft_output.resize(static_cast<size_t>(N) * 2);

    for (int k = 0; k < N; k++) {
        iq_data_d[2 * k]     = static_cast<double>(ddc_iq[2 * k]);
        iq_data_d[2 * k + 1] = static_cast<double>(ddc_iq[2 * k + 1]);
    }

    // [CHANGE] 실제 길이 N으로 FFT 실행 (내부에서 길이가 다르면 plan 재생성)
    _fft_executor.execute(iq_data_d.data(), fft_output.data(), N);

    // [CHANGE] 평균 스펙트럼 버퍼 길이 보장 (길이가 달라지면 재할당/초기화)
    if (_avg_spec.size() != static_cast<size_t>(N)) {
        _avg_spec.assign(static_cast<size_t>(N), 0.0);
        _avg_count = 0;
    }

    double fft_level_offset = 20.0 * log10(static_cast<double>(N));
    for (int i = N / 2; i < N; i++) {
        double real = fft_output[2 * i];
        double imag = fft_output[2 * i + 1];

        if (real == 0) real += 1e-7;
        if (imag == 0) imag += 1e-7;

        double power = 10.0 * log10(real * real + imag * imag) + _config_data.level_offset - fft_level_offset;

        if (max_power_level < power)
            max_power_level = power;

        _avg_spec[static_cast<size_t>(i - (N / 2))] += power / 64.0;
    }

    for (int i = 0; i < N / 2; i++) {
        double real = fft_output[2 * i];
        double imag = fft_output[2 * i + 1];

        if (real == 0) real += 1e-7;
        if (imag == 0) imag += 1e-7;

        double power = 10.0 * log10(real * real + imag * imag) + _config_data.level_offset - fft_level_offset;

        if (max_power_level < power)
            max_power_level = power;

        _avg_spec[static_cast<size_t>(i + (N / 2))] += power / 64.0;
    }

    return max_power_level;
}

void NpuWatchProcess::initFile() {

    _encoder = std::make_unique<IFDataFormatEncoder>(_watchFileSaveInfo.samplerate,
                                                     _watchFileSaveInfo.frequency,
                                                     _watchFileSaveInfo.bandwidth,
                                                     _watchFileSaveInfo.starttime / 1000);

    // file save start
    _file_writer->init(_watchFileSaveInfo._output_path.string(),
                       _watchFileSaveInfo.frequency,
                       _watchFileSaveInfo.samplerate,
                       _watchFileSaveInfo.bandwidth,
                       _watchFileSaveInfo.starttime / 1000);
}

void NpuWatchProcess::writeToFile(const float* ddc_iq, const int samples) {
    _encoder->setAntennaVoltageRef(_config_data.ant_volt_ref);
    auto encoded = _encoder->encodeIQData((const char*)ddc_iq, samples * sizeof(float) * 2);

    _watchFileSaveInfo._outDDCFile.write(encoded->data(), encoded->size());
    _file_writer->write(ddc_iq, samples);
}

void NpuWatchProcess::closeFile() { 
    _watchFileSaveInfo._outDDCFile.close();
    _file_writer->close();
}

bool NpuWatchProcess::saveDatabase() {
    std::string db_ipaddress = _config->getValue("DB.IPADDRESS");
    std::string db_sid = _config->getValue("DB.SID");
    std::string db_user = _config->getValue("DB.USER");
    std::string db_password = _config->getValue("DB.PASSWORD");

    NpuWatchResult r;
    r.filepath = _watchFileSaveInfo._filepath;
    r.frequency = _watchFileSaveInfo.frequency;
    r.bandwidth = _watchFileSaveInfo.bandwidth;
    r.samplerate = _watchFileSaveInfo.samplerate;
    r.starttime = _watchFileSaveInfo.starttime / 1000;
    r.endtime = ceil(_watchFileSaveInfo.endttime / 1000);
    r.guid = generateID();
    r.power_level = _max_channel_power;

    OracleDBAccess db_access(db_ipaddress
                             , db_sid
                             , db_user
                             , db_password);

    if (!db_access.initialize()) {
        std::cerr << "Oracle::DBAccess::initailize returns false" << std::endl;
        return false;
    }

    std::string mission_id = r.guid;
    int device_id = 1;
    int64_t fc = r.frequency;
    int bw = r.bandwidth;
    int fs = r.samplerate;
    time_t starttime = r.starttime;
    time_t endtime = r.endtime;
    double power_level = r.power_level;
    int demod_type = 99;
    double threshold = 0;
    int holdtime = 0;
    int continuetime = 0;
    int antport = 0;
    std::string comment = "";
    std::string filepath = convertPathToWindowsFormat(r.filepath);

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

void NpuWatchProcess::initFileSaveInfo(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate) {
    
    boost::filesystem::path watch_dir(_config_data.output_path + "watch" + "/");
    _watchFileSaveInfo._output_path = boost::filesystem::path(watch_dir.string() + _watch_info.guid);
    // _watchFileSaveInfo._output_path = boost::filesystem::path(_config_data.output_path + _watch_info.guid);

    boost::system::error_code ec;
    if (!boost::filesystem::create_directory(_watchFileSaveInfo._output_path, ec)) {
        std::cerr << "Failed to create ddc result directory - " << ec.message() << std::endl;
    }

    _watchFileSaveInfo.frequency = frequency;
    _watchFileSaveInfo.bandwidth = bandwidth;
    _watchFileSaveInfo.samplerate = samplerate;
    _watchFileSaveInfo.starttime = timestamp;
    _watchFileSaveInfo.endttime = timestamp;

    std::string sitename = _config->getValue("SITE_NAME");
    NpuBasicFileNameFormatter filename_formatter(sitename);
    _watchFileSaveInfo._fileName = filename_formatter.getFileName(_watchFileSaveInfo.frequency,
                                                                  _watchFileSaveInfo.samplerate,
                                                                  _watchFileSaveInfo.bandwidth,
                                                                  _watchFileSaveInfo.starttime / 1000);
    _watchFileSaveInfo._filepath = _watchFileSaveInfo._output_path.string() + "/" + _watchFileSaveInfo._fileName + ".dat";
    _watchFileSaveInfo._outDDCFile.open(_watchFileSaveInfo._filepath, std::ios::binary | std::ios::out);
}

void NpuWatchProcess::updateFileSaveInfo(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate) {
    _watchFileSaveInfo.frequency = frequency;
    _watchFileSaveInfo.bandwidth = bandwidth;
    _watchFileSaveInfo.samplerate = samplerate;
    _watchFileSaveInfo.endttime = timestamp;
}

void NpuWatchProcess::onIQDataReceived(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate, const float* ddc_iq, const size_t ddc_samples) {
    double sample_time = ddc_samples / (double)samplerate;
    double power_level = getPowerLevel(ddc_iq, ddc_samples) - 175.0;

    if (_state == EnWatchState::DEAD) {
        if (power_level > _watch_req.threshold) {
            _state = EnWatchState::ALIVE;
            _holdtime = 0.0;
            _max_channel_power = power_level;

            initFileSaveInfo(timestamp, frequency, bandwidth, samplerate);
            initFile();
            writeToFile(ddc_iq, static_cast<int>(ddc_samples));
            updateFileSaveInfo(timestamp, frequency, bandwidth, samplerate);
        }
    } else if (_state == EnWatchState::ALIVE) {
        if (power_level < _watch_req.threshold) {
            _state = EnWatchState::HOLD;
            _holdtime += sample_time;
        } else {
            if (power_level > _max_channel_power) {
                _max_channel_power = power_level;
            }
        }

        if (timestamp - _watchFileSaveInfo.starttime > _watch_req.continuetime * 1000) {
            closeFile();
            saveDatabase();
            initFileSaveInfo(timestamp, frequency, bandwidth, samplerate);
            initFile();
        }

        writeToFile(ddc_iq, static_cast<int>(ddc_samples));
        updateFileSaveInfo(timestamp, frequency, bandwidth, samplerate);
        if (power_level > _max_channel_power) {
            _max_channel_power = power_level;
        }
    } else if (_state == EnWatchState::HOLD) {
        if (power_level > _watch_req.threshold) {
            if (power_level > _max_channel_power) {
                _max_channel_power = power_level;
            }

            _state = EnWatchState::ALIVE;
            _holdtime = 0.0;

            writeToFile(ddc_iq, static_cast<int>(ddc_samples));
            updateFileSaveInfo(timestamp, frequency, bandwidth, samplerate);

        } else {
            _holdtime += sample_time;
            if (_holdtime >= _watch_req.holdtime) {
                _state = EnWatchState::DEAD;
                //writeToFile(ddc_iq, ddc_samples);
                //updateFileSaveInfo(timestamp, frequency, bandwidth, samplerate);
                saveDatabase();
                closeFile();
            } else {
                writeToFile(ddc_iq, static_cast<int>(ddc_samples));
                updateFileSaveInfo(timestamp, frequency, bandwidth, samplerate);
            }
        }
    }

    if (_avg_count == 64) {
        boost::filesystem::path spec_dir(_watchFileSaveInfo._output_path.string() + "/" + "spec");
        boost::system::error_code ec;
        boost::filesystem::create_directory(spec_dir, ec);

        std::string spec_file_path = spec_dir.string() + "/" + "spec.csv";
        std::ofstream spec_file;
        spec_file.open(spec_file_path, std::ios::trunc);

        if (!spec_file.good())
            return;

        for (int i = 0; i < static_cast<int>(ddc_samples); i++) {
            spec_file << _avg_spec[static_cast<size_t>(i)] << "\n";
        }

        spec_file.close();

        _avg_count = 0;
        // [CHANGE] 안전 리셋: memset 대신 assign 사용
        _avg_spec.assign(_avg_spec.size(), 0.0);
    } else {
        _avg_count++;
    }
}

void NpuWatchProcess::close() {
    if (_watchFileSaveInfo._outDDCFile.good() && _watchFileSaveInfo._outDDCFile.is_open()) {
        closeFile();
        saveDatabase();
    }
}

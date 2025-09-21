#include "NpuDFjob.h"

#include "DataStorageInfo.h"
#include "DataStorageSearcher.h"
#include "NpuBasicFileNameFormatter.h"
#include "NpuConfigure.h"
#include "NpuExtract16tWriter.h"
#include "NpuExtractWavWriter.h"
#include "IFDataFormatEncoder.h"
#include "OracleDBAccess.h"
#include "RestClient.h"
#include "util.hpp"
#include <random>

#include <boost/make_unique.hpp>

NpuDFJob::NpuDFJob(const NpuDFRequest& df_req, NpuConfigure* config, std::unique_ptr<DataStorageInfo> data_storage_info)
    : _df_req(df_req)
    , _config(config)
    , _running(false)
    , _dfProcess(config, df_req)
    , _dat_storage_info(std::move(data_storage_info))
{
    _df_info.frequency = df_req.frequency;
    _df_info.threshold = df_req.threshold;
    _df_info.bandwidth = df_req.bandwidth;
    _df_info.starttime = df_req.starttime;
    _df_info.endtime = df_req.endtime;
    _df_info.progress = 0;
}

NpuDFJob::~NpuDFJob() {
}

std::string NpuDFJob::start(JOB_COMPLEDTED_CALLBACK completed_callback) {
    boost::lock_guard<boost::mutex> lock(_mtx);

    if (_running)
        return _df_info.guid;

    _dfProcess.loadConfig();
    _df_info.guid = generateID();
    _completed_callback = completed_callback;
    _running = true;
    _thread.reset(new std::thread(std::bind(&NpuDFJob::work, shared_from_this())));

    return _df_info.guid;
}

void NpuDFJob::stop() {
    _running = false;
}

void NpuDFJob::wait() {
    if (_thread && _thread->joinable()) {
        _thread->join();
    }
}

void NpuDFJob::work() {

    std::vector<std::complex<double>> cal = _dfProcess.loadCalibrationFile();
    std::vector<std::complex<double>> caled;
    std::vector<std::complex<double>> caledTemp;

    if(_dfProcess.test)
    {
        //_dfProcess.testRawDataRead();
        _dfProcess.testDataRead();
        caled = _dfProcess.TestCalDF;
        //caled = _dfProcess.calibrationNormalize(_dfProcess.TestCalRawDF);
        //caled.push_back(std::complex<double>(1.0,0.0));
        //caled.push_back(std::complex<double>(0.31381357083820360, 0.75481654041957458));
        //caled.push_back(std::complex<double>(0.72255884693394135, -0.46095188780401247));
        //caled.push_back(std::complex<double>(-0.76868384205539697, -0.22837306671991439));
        //caled.push_back(std::complex<double>(0.79068066725116082, -0.17444744102703713));
    }
    else
    {
        //caledTemp = _dfProcess.calibrationNormalize(cal);
        //caled.push_back(std::complex<double>(1.0,0.0));
        //caled.push_back(std::complex<double>(-0.780530677516302, -0.299182418788505));
        //caled.push_back(std::complex<double>(0.385713920370994, -0.785964545869155));
        //caled.push_back(std::complex<double>(0.132593282493555, 0.850706952400915));
        //caled.push_back(std::complex<double>(0.040034388686934, -0.790953338342709));

        //caled.push_back(std::complex<double>(1.0,0.0));
        //caled.push_back(std::complex<double>(-0.760314535212447 , -0.339620547400163));
        //caled.push_back(std::complex<double>(0.373874031143077, -0.812809859646673));
        //caled.push_back(std::complex<double>(0.191389182100246, 0.824111569587052));
        //caled.push_back(std::complex<double>(0.0288353297734161, -0.787328618848319));

        caled.push_back(std::complex<double>(1.0,0.0));
        caled.push_back(std::complex<double>(-1.893258427, -1.129213483));
        caled.push_back(std::complex<double>(-2.003773585, -0.286792453));
        caled.push_back(std::complex<double>(1.642355009, 2.257469244));
        caled.push_back(std::complex<double>(-0.185334408, 1.121676068));
    }

    int size = _dfProcess.mfData.size();
    if(_dfProcess.mfData.size() > 5 * 361)
    {
        // do interpolation
        double alpha = (_df_req.frequency / 1e6 - _dfProcess.l_freq) / (_dfProcess.h_freq - _dfProcess.l_freq);
        for(int azi = 0; azi < 361; ++azi) {
            std::vector<std::complex<double>> mfData;
            for(int ant = 0; ant < 5; ++ant) {
                size_t idx_low = azi * 5 + ant;
                size_t idx_high = (361 + azi) * 5 + ant;

                std::complex<double> low, high, debugTemp;
                low = _dfProcess.complexMulReal(_dfProcess.mfData[idx_low], 1.0-alpha);
                high = _dfProcess.complexMulReal(_dfProcess.mfData[idx_high], alpha);
                mfData.push_back((low+high) * caled[ant]);
                debugTemp = low+high;
            }
            _dfProcess.streeVector.push_back(mfData);
        }
    }
    else
    {
        // not interpolation
        for(int azi = 0; azi < 361; ++azi) {
            std::vector<std::complex<double>> mfData;
            for(int ant = 0; ant < 5; ++ant) {
                mfData.push_back(_dfProcess.mfData[azi * 5 + ant] * caled[ant]);
            }
            _dfProcess.streeVector.push_back(mfData);
        }
    }

    bool mfFileTest = true;
    if(mfFileTest == true)
    {
        std::string fileName = "/data/ddcTest/mfTest/MF.bin";
        std::ofstream outFileMF(fileName, std::ios::binary);
        int size = _dfProcess.mfData.size() * 16;
        outFileMF.write(reinterpret_cast<const char*>(_dfProcess.mfData.data()), size); //
        outFileMF.close();

        fileName = "/data/ddcTest/mfTest/CaledMF.bin";
        int ROWS = 361;
        int COLS = 5;
        int NUM = ROWS * COLS;
        std::vector<std::complex<double>> buf;
        buf.reserve(NUM);
        for (int i=0; i<_dfProcess.streeVector.size(); i++)
        {
            for (size_t r = 0; r < ROWS; ++r)
                buf.insert(buf.end(), _dfProcess.streeVector[r].begin(), _dfProcess.streeVector[r].end());
        }
        std::ofstream ofs(fileName, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(buf.data()), NUM * 16);

        int debug = 1;
    }


    while (true) {
        // lock
        {
            boost::lock_guard<boost::mutex> lock(_mtx);
            if (!_running)
                break;
        }

        if (_df_info.starttime >= _df_info.endtime) {
            std::cerr << "Invalid time range" << std::endl;
            break;
        }

        if (_df_info.starttime < _dat_storage_info->starttime) {
            std::cerr << "Invalid starttime" << std::endl;
            break;
        }

        if (_df_info.endtime > _dat_storage_info->endtime) {
            std::cerr << "Invalid endtime" << std::endl;
            break;
        }


        int64_t start_sample_pos = (_df_info.starttime - _dat_storage_info->starttime) * _dat_storage_info->samplerate;
        int64_t end_sample_pos = (_df_info.endtime - _dat_storage_info->starttime) * _dat_storage_info->samplerate;
        int64_t sample_count = end_sample_pos - start_sample_pos;
        double frame_count = sample_count / (double)_dat_storage_info->frame_samples;

        std::cout << "start_sample_pos : " << start_sample_pos << std::endl;
        std::cout << "end_sample_pos : " << end_sample_pos << std::endl;
        std::cout << "frame_samples : " << _dat_storage_info->frame_samples << std::endl;
        std::cout << "sample_count : " << sample_count << std::endl;
        std::cout << "frame_count : " << frame_count << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        extractIQ(_dat_storage_info.get());

        auto end_time = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> elapsed = end_time - start_time;
        _df_info.progress = 100.0;
        std::cout << "Execution time: " << elapsed.count() << " sec" << std::endl;

        break;
    }

    if (_completed_callback)
        _completed_callback(_df_info.guid);
}

void NpuDFJob::extractIQ(const DataStorageInfo* dat_storage_info) {
    if (!_ddc_exe.initDDC(_df_info.starttime, _df_info.endtime, _df_info.frequency, _df_info.bandwidth, dat_storage_info))
        return;

    if (!_ddc_exe.initSecondaryDDC(_df_info.starttime, _df_info.endtime, _df_info.frequency, _df_info.bandwidth, dat_storage_info))
        return;

    unsigned int channel_samplerate = _ddc_exe.getChannelSamplerate();
    unsigned int channel_bandwidth = _ddc_exe.getChannelBandwidth();
    unsigned int ddc_samples = _ddc_exe.getDDCSamples();

    _df_info.samplerate = channel_samplerate;

    _dfProcess.init(_df_req, _df_info, ddc_samples);
    while (_running && _ddc_exe.executeDDCDF(this)) {
        // do nothing
    }

    _dfProcess.close();
    _ddc_exe.close();
}

NpuDFInfo NpuDFJob::getDFInfo()
{
    return _df_info;
}


void NpuDFJob::onInitialized() {
    // TODO : Implement
}

void NpuDFJob::onClosed() {
    // TODO : Implement
}

void NpuDFJob::onReadFrame(int total_frame, int frame_num) {
}

void NpuDFJob::onFullBuffer(time_t timestamp, float* ddc_iq, size_t ddc_samples) {
    double duration = _df_req.endtime - _df_req.starttime;
    _df_info.progress = ((timestamp / 1000) - (double)_df_req.starttime) / duration * 100;

    vaildation(timestamp, ddc_iq, ddc_samples);

    if (true)
    {
        NpuDFList dfData;
        dfData.frequency = _df_info.frequency;
        dfData.bandwidth = _df_info.bandwidth;
        dfData.startTime = timestamp / 1000;
        dfData.endTime = timestamp / 1000 + 10;

        std::random_device rd;
        std::mt19937 gen(rd()); // Mersenne Twister 19937 엔진

        // 분포 정의
        std::uniform_real_distribution<double> dist(320.0, 350.0); // 0~1 실수

        dfData.Azimuth = dist(gen);
        _df_info.dfResults.push_back(dfData);

        double azi = 0.0;
        double spec[361];
        memset(&spec, 0, sizeof(double) * 361);

        int df_samples = ddc_samples / 2;
        _dfProcess.directionFinding(ddc_iq, &azi, spec, df_samples);

        std::string time = std::to_string(timestamp);
        std::string fileName = "/data/ddcTest/space/music_" + time + ".bin";
        std::ofstream outFile(fileName, std::ios::binary);
        outFile.write(reinterpret_cast<const char*>(spec), 361 * sizeof(double));
        outFile.close();

        std::cout << "azimuth: " << azi << std::endl;
    }

    std::cout << _df_info.progress << std::endl;

    //_dfProcess.onIQDataReceived(timestamp, _df_req.frequency, _ddc_exe.getChannelBandwidth(), _ddc_exe.getChannelSamplerate(), ddc_iq, ddc_samples);
}

void NpuDFJob::vaildation(time_t timestamp, float* ddc_iq, size_t ddc_samples) {

    std::string saveDir = "/data/ddcTest/";
    std::string dir = "";
    std::string name = "ddcTest";
    std::string ext = ".bin";
    std::string time = std::to_string(timestamp);

    //for(int i=0; i<ddc_samples; i++)
    //{
    //    std::cout << std::to_string(ddc_iq[i]) << std::endl;
    //}

    int ddc_half_samples = ddc_samples; // remove dummy sample
    dir = saveDir + name + "DFDDC" + "_" + time + "_total" +ext;
    std::cout << saveDir << std::endl;
    std::ofstream outFile(dir.data(), std::ios::binary);
    outFile.write(reinterpret_cast<const char*>(ddc_iq), ddc_half_samples * sizeof(float) * 8);
    outFile.close();

    if(false)
    {
        int num = 1;
        int ddc_iq_index = 0;
        float * ddcResult = new float[ddc_samples * num];
        for(int i=0; i<8; i++)
        {
            ddc_iq_index = i * ddc_samples * num;
            std::memset(&ddcResult[0], 0, ddc_samples * num * sizeof(float));
            std::memcpy(&ddcResult[0], &ddc_iq[ddc_iq_index], ddc_half_samples * sizeof(float));

            if(i%2 == 0)
            {
                dir = saveDir + name + "ddc_Ach_" + std::to_string(i) + "_" + time + ext;
                std::ofstream outFile(dir.data(), std::ios::binary);
                outFile.write(reinterpret_cast<const char*>(ddcResult), ddc_half_samples * sizeof(float));
                outFile.close();
            }
            else
            {
                dir = saveDir + name + "ddc_Bch_" + std::to_string(i) + "_" + time + ext;
                std::ofstream outFile(dir.data(), std::ios::binary);
                outFile.write(reinterpret_cast<const char*>(ddcResult), ddc_half_samples * sizeof(float));
                outFile.close();
            }

            std::cout << "ddcResult: " << std::to_string(i) << std::endl;
        }
        delete[] ddcResult;
    }

    int debug = 1;
}

int NpuDFJob::loadMFPoint(double * freqdata) {
    int _lowBand = 77;
    int _midBand = 73;
    int _highBand = 141;
    int mf_freq_len = _lowBand + _midBand + _highBand;

    double freq = _df_req.frequency / 1e6;

    int fi = 0;
    while (fi < mf_freq_len - 1 && freq > freqdata[fi + 1])
        ++fi;

    return fi;
}


void NpuDFJob::setMFData(std::vector<std::complex<double>> Data) {
    _dfProcess.setmfData(Data);
}

void NpuDFJob::setInterFreq(float hfreq, float lfreq) {
    _dfProcess.setInterpolationFreq(hfreq, lfreq);
}














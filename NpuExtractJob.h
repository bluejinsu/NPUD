#ifndef NPU_EXTRACT_JOB_H
#define NPU_EXTRACT_JOB_H


#include "DDCExecutor.h"
#include "DDCHandler.h"
#include "FFTExecutor.h"
#include "NpuExtractResult.h"
#include "NpuExtractInfo.h"
#include "NpuExtractRequest.h"
#include "IFDataFormatEncoder.h"

#include <boost/make_unique.hpp>
#include <boost/thread.hpp>
#include <opencv4/opencv2/opencv.hpp>

#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct DataStorageInfo;
class INpuExtractFileWriter;
class INpuFileNameFormatter;
class NpuConfigure;

struct stExtractConfigData {
	double level_offset;
	double ant_volt_ref;
	std::string output_path;
};

class NpuExtractJob : public IDDCHandler, public std::enable_shared_from_this<NpuExtractJob> {
public:
	typedef std::function<void(const std::string&)> JOB_COMPLEDTED_CALLBACK;

public:
	NpuExtractJob(const NpuExtractRequest& ext_req, NpuConfigure* config, std::unique_ptr<DataStorageInfo> data_storage_info);
	~NpuExtractJob();
	
	std::string start(JOB_COMPLEDTED_CALLBACK completed_callback);
	void stop();
	void wait();
	const NpuExtractInfo getExtractInfo() const { return _ext_info; }
	
public:
	virtual void onInitialized() override;
    virtual void onClosed() override;
	virtual void onReadFrame(int total_frame, int frame_num) override;
	virtual void onFullBuffer(time_t timestamp, float* ddc_iq, size_t ddc_samples) override;

private:
	void work();
	std::unique_ptr<NpuExtractResult> extractIQ(const DataStorageInfo* dat_storage_info, INpuExtractFileWriter* file_writer, INpuFileNameFormatter* filename_formatter);
		
	void loadConfig();
	
private:
	NpuExtractRequest _ext_req;
	NpuExtractInfo _ext_info;
	NpuConfigure* _config;
	std::unique_ptr<DataStorageInfo> _dat_storage_info;

	stExtractConfigData _config_data;

	boost::mutex _mtx;
	std::unique_ptr<std::thread> _thread;
	bool _running;

	JOB_COMPLEDTED_CALLBACK _completed_callback;

	FFTExecutor _fft_executor;
	double _max_channel_power;
	INpuExtractFileWriter* _file_writer;
	std::unique_ptr<IFDataFormatEncoder> encoder;
	std::ofstream outDDCFile;
	boost::filesystem::path output_path;

	std::string fileName;
	std::string filepath;

	DDCExecutor _ddc_exe;

	cv::Mat _thumb_image;
	std::vector<std::vector<double>> _spec_lines;

};

#endif

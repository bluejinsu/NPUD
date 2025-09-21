 #include "FFTExecutor.h"
#include "IFDataFormatEncoder.h"
#include "INpuExtractFileWriter.h"
#include "NpuBasicFileNameFormatter.h"
#include "NpuConfigure.h"

#include "DDCExecutor.h"
#include "INpuFileNameFormatter.h"

#include "NpuDFInfo.h"
#include "NpuDFRequest.h"


#include <boost/filesystem.hpp>

#include <fstream>
#include <memory>
#include <vector>

#define MAX_SVD 5
#define MUSIC_FINE_WINDOW_DEG      2.0      /* fine 스캔 반창(±) */
#define MUSIC_FINE_STEP_DEG        0.1      /* fine 스텝 */
#define MUSIC_SUPPRESS_DEG         3.0      /* coarse 씨앗 최소 간격(도) */
#define MUSIC_MIN_SEPARATION_DEG   1.0      /* 최종 피크 최소 간격 */
#define MUSIC_COARSE_WEIGHT_ALPHA  0.5      /* coarse 가중치 지수 α */
#define MUSIC_SEED_MULTIPLIER      3        /* num_seeds = min(3*K, 20) */
#define MUSIC_SEED_CAP             20       /* 씨앗 상한 */
#define PI 3.14


/* 고정 상한(안전 마진) */
#define MAX_SEEDS      (MUSIC_SEED_CAP)
#define MAX_FINE_STEPS 401  /* 대략 (2*2deg)/0.1deg + 여유 ≈ 41 → 넉넉히 401 */
#define MAX_CANDS      (MAX_SEEDS * 64) /* seed 당 후보가 많아도 안전하게 */

typedef struct dcplx {
    double re;
    double im;
} dcplx;

typedef struct {
    double deg;
    double P;
    int seed_deg;
    double score;
} PeakCand_c;

struct stDFConfigData {
    double level_offset;
    double ant_volt_ref;
    std::string output_path;
};


struct DFFileSaveInfo {
    time_t starttime;
    time_t endttime;
    int64_t frequency;
    int samplerate;
    int bandwidth;
    std::ofstream _outDDCFile;
    boost::filesystem::path _output_path;
    std::string _fileName;
    std::string _filepath;
};


class NpuConfigure;

class NpuDFProcess {
public:
    NpuDFProcess (NpuConfigure* config, NpuDFRequest req);
    ~NpuDFProcess();

    void init(const NpuDFRequest& df_req, const NpuDFInfo& df_info, const size_t ddc_samples);

    void close();

    void loadConfig();
    void onIQDataReceived(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate, const float* ddc_iq, const size_t ddc_samples);

    void setmfData(std::vector<std::complex<double>> Data);
    void setInterpolationFreq(float hfreq, float lfreq);

    std::vector<std::complex<double>> loadCalibrationFile();
    std::vector<std::complex<double>> calibrationNormalize(std::vector<std::complex<double>> data);
    std::vector<std::complex<double>> antNormalize(float *data, int ddcSample);

    //testCode
    bool test = false;
    void testDataRead();
    void testRawDataRead();
    std::vector<std::complex<double>> TestAntDF;
    std::vector<std::complex<double>> TestCalDF;

    std::vector<std::complex<double>> TestAntRawDF;
    std::vector<std::complex<double>> TestCalRawDF;

    // linear interpolation data;
    float h_freq;
    float l_freq;
    std::vector<std::complex<double>> mfData;
    std::vector<std::vector<std::complex<double>>> streeVector;
    std::complex<double> complexMulReal(std::complex<double> mf, double r);

    void directionFinding(float * antData, double * azi, double * musicSpec, int ddcSample);
    void getCovMatrix(const dcplx* FIFO_DF, int N, int interleavedFlag, dcplx (*R)[MAX_SVD]);
    void getSVD(const dcplx R_in[MAX_SVD][MAX_SVD], double S[MAX_SVD], dcplx  U[MAX_SVD][MAX_SVD], dcplx  V[MAX_SVD][MAX_SVD]);
    void getMUSIC(const dcplx(*Manifold)[MAX_SVD], const dcplx U[MAX_SVD][MAX_SVD], double *dEst_Azi, double *dMUSIC_Spec, int sig_num);

    void jacobi_sym(double M[][2*MAX_SVD], double Z[][2*MAX_SVD], double d[2*MAX_SVD], int n2);
    void matv_mul_Qa_c(const dcplx Q[MAX_SVD][MAX_SVD], const dcplx* a, dcplx* y, int n);
    dcplx c_dot_h_c(const dcplx* a, const dcplx* b, int n);
    void sort_candidates_by_score_desc_c(PeakCand_c* a, int n);
    int too_close_deg_c(double a, double b, double sep);
    int CollectFineCandidates_c(const dcplx Q[MAX_SVD][MAX_SVD], const dcplx(*Manifold)[MAX_SVD],int center_deg, double halfW, double step, PeakCand_c* out,int out_cap);
    void BuildA_FromLUT_PhaseInterp_c(const dcplx(*Manifold)[MAX_SVD], int idx0, double t, dcplx* out /*[MAX_SVD]*/);
    void to_polar_c(dcplx z, double* mag, double* ph);
    double unwrap_to_prev_c(double prev, double curr);

private:
    double getPowerLevel(const float* ddc_iq, size_t ddc_samples);

    void initFile();
    void writeToFile(const float* ddc_iq, const int samples);
    void closeFile();
    void initFileSaveInfo(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate);
    void updateFileSaveInfo(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate);
    bool saveDatabase();

    // DF Data Calculator


private:
    NpuDFRequest _df_req;
    NpuDFInfo _df_info;
    FFTExecutor _fft_executor;

    int _sample;
    double _max_channel_power;
    double _holdtime;

    NpuBasicFileNameFormatter _filename_formatter;
    INpuExtractFileWriter* _file_writer;
    std::unique_ptr<IFDataFormatEncoder> _encoder;

    std::vector<std::vector<double>> _spec_lines;
    std::vector<double> _avg_spec;
    int _avg_count;

    DFFileSaveInfo _dfFileSaveInfo;

    NpuConfigure* _config;
    stDFConfigData _config_data;

    enum EnDFState {
        DEAD,
        ALIVE,
        HOLD,
    } _state;
};



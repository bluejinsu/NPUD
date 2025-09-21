#include "NpuDFProcess.h"
#include "NpuConfigure.h"

#include "NpuExtract16tWriter.h"
#include "NpuExtractWavWriter.h"
#include "FileReader.h"

#include "dirent.h"
#include "sys/stat.h"
#include <complex>

NpuDFProcess::NpuDFProcess(NpuConfigure* config, NpuDFRequest req)
:_config(config)
    , _avg_count(0)
    , _df_req(req)
    , _max_channel_power(-1000)
    , _state(EnDFState::DEAD)
    , _sample(1024)
    , _filename_formatter(config->getValue("SITE_NAME"))
{
}

NpuDFProcess::~NpuDFProcess() { }

void NpuDFProcess::loadConfig() {
    _config_data.level_offset = atof(_config->getValue("DATA.LEVEL_OFFSET").c_str());
    _config_data.ant_volt_ref = atof(_config->getValue("DATA.ANT_VOLTAGE_REF").c_str());
    std::string output_path = _config->getValue("EXTRACT_OUTPUT_PATH");
    _config_data.output_path = output_path;
}

void NpuDFProcess::init(const NpuDFRequest& df_req, const NpuDFInfo& df_info, const size_t ddc_samples) {
    _df_info = df_info;
    _df_req = df_req;

    std::unique_ptr<INpuExtractFileWriter> file_writer;

    if (_df_req.filetype == "16t") {
        file_writer.reset(new NpuExtract16tWriter(&_filename_formatter));
    } else if (_df_req.filetype == "wav") {
        file_writer.reset(new NpuExtractWavWriter(&_filename_formatter));
    }


    _file_writer = new NpuExtract16tWriter(&_filename_formatter);
    _fft_executor.init(ddc_samples);

    _dfFileSaveInfo._output_path = boost::filesystem::path(_config_data.output_path + _df_info.guid);
    boost::filesystem::path spec_dir(_dfFileSaveInfo._output_path.string() + "/" + "spec");
    boost::system::error_code ec;
    if (!boost::filesystem::create_directory(_dfFileSaveInfo._output_path, ec)) {
        std::cerr << "Failed to create ddc result directory - " << ec.message() << std::endl;
    } else {
        boost::filesystem::create_directory(spec_dir, ec);
    }
}

void NpuDFProcess::onIQDataReceived(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate, const float* ddc_iq, const size_t ddc_samples) {

    // nothing


}

void NpuDFProcess::close() {

}

void NpuDFProcess::setmfData(std::vector<std::complex<double>> Data) {
    mfData = Data;
}

std::vector<std::complex<double>> NpuDFProcess::loadCalibrationFile()
{
    FileReader file;
    FileInfo base;
    base.epoch = 0;
    //std::string calPath = _config->getValue("CALIBRATION_DATA_PATH") + "ch" + std::to_string(_df_req.instance) + "/";
    std::string calPath = _config->getValue("CALIBRATION_DATA_PATH") + "ch1" + "/";


    struct dirent** nameList = nullptr;
    int n = scandir(calPath.c_str(), &nameList, nullptr, alphasort);

    for(int i=0; i<n; i++)
    {
        FileInfo info;
        struct dirent* ent = nameList[i];

        const char * name = ent->d_name;
        if(name[0] == '.' && (name[0] == '\0' || name[1] == '.' && name[2] == '\0' ))
        {
            free(ent);
            continue;
        }

        if(file.parseCalFileName(name, info, _df_req.frequency)) {
            if(info.epoch > base.epoch)
            {
                base = info;
            }
        }
    }


    calPath = calPath + base.path;
    file.Open(calPath);

    std::vector<float> calFloatData;
    calFloatData.resize(file.FileSize() / sizeof(float));
    file.Read(calFloatData.data(), file.FileSize());

    std::vector<std::complex<double>> calData;
    for(int i=0; i<calFloatData.size() / 2; i++)
    {
        calData.push_back(std::complex<double>(calFloatData[i*2], calFloatData[i*2+1]));
    }

    return calData;
}

void NpuDFProcess::testDataRead() {
    FileReader read;
    std::string antNormPath = "/data/ddcTest/dfTest/ant1_angle140_ant405M.bin";
    std::string calNormPath = "/data/ddcTest/dfTest/ant1_cal405M.bin";

    std::vector<double> fileData;
    fileData.resize(8192);
    read.Open(antNormPath);
    read.Read(fileData.data(), read.FileSize());
    read.Close();

    for(int i=0; i<4096; i++)
    {
        TestAntDF.push_back(std::complex<double>(fileData[i*2], fileData[i*2+1]));
    }

    fileData.resize(10);
    read.Open(calNormPath);
    read.Read(fileData.data(), read.FileSize());
    read.Close();

    for(int i=0; i<5; i++)
    {
        TestCalDF.push_back(std::complex<double>(fileData[i*2], fileData[i*2+1]));
    }

    int debug = 0;
}

void interleave_AB(const std::vector<std::complex<double>>& in,
                   std::vector<std::complex<double>>& out,
                   size_t rows_per_group = 4, size_t cols = 1024)
{
    const size_t Arows = rows_per_group;         // 4
    const size_t Brows = rows_per_group;         // 4
    const size_t total_rows = Arows + Brows;     // 8

    out.resize(total_rows * cols);

    for (size_t r = 0; r < rows_per_group; ++r) {
        // A의 r행 -> 목적지 2*r 행
        std::copy(in.begin() + (r * cols),
                  in.begin() + ((r + 1) * cols),
                  out.begin() + ((2 * r) * cols));

        // B의 r행 (소스에서 4+r행) -> 목적지 2*r+1 행
        std::copy(in.begin() + ((Arows + r) * cols),
                  in.begin() + ((Arows + r + 1) * cols),
                  out.begin() + ((2 * r + 1) * cols));
    }
}

void NpuDFProcess::testRawDataRead() {
    FileReader read;
    std::string antRawPath = "/data/ddcTest/dfTest/rawAnt.bin";
    std::string calRawPath = "/data/ddcTest/dfTest/rawCal.bin";

    std::vector<double> fileData;
    read.Open(antRawPath);

    fileData.resize(read.FileSize() / sizeof(double));
    read.Read(fileData.data(), read.FileSize());
    read.Close();

    std::vector<std::complex<double>> temp;
    for(int i=0; i<fileData.size() / 2; i++)
    {
        temp.push_back(std::complex<double>(fileData[i*2], fileData[i*2+1]));
    }
    interleave_AB(temp, TestAntRawDF, 4, 1024);

    read.Open(calRawPath);
    fileData.resize(read.FileSize() / sizeof(double));
    read.Read(fileData.data(), read.FileSize());
    read.Close();

    temp.clear();
    for(int i=0; i<fileData.size() / 2; i++)
    {
        temp.push_back(std::complex<double>(fileData[i*2], fileData[i*2+1]));
    }
    interleave_AB(temp, TestCalRawDF, 4, 1024);

    int debug = 0;
}

std::vector<std::complex<double>> NpuDFProcess::calibrationNormalize(std::vector<std::complex<double>> data) {
    std::vector<std::complex<double>> calibNormData;
    calibNormData.push_back(std::complex<double>(1.0, 0.0));

    int dataSize = data.size();
    std::complex<double> Ach;
    std::complex<double> Bch;
    int index = 512;
    for(int i=0; i<4; i++)
    {
        Ach = data[index];
        index += 1024;

        Bch = data[index];
        index += 1024;

        calibNormData.push_back(std::complex<double>(Bch / Ach));
    }

    return calibNormData;
}

std::vector<std::complex<double>> NpuDFProcess::antNormalize(float *data, int ddcSample) {

    std::vector<std::complex<double>> Ach;
    std::vector<std::complex<double>> Bch;
    std::vector<std::complex<double>> result;

    int index = 0;
    int step = ddcSample;
    for(int i=0; i < 8; i++)
    {
        for (int k = 0; k < ddcSample; k++)
        {
            if(i % 2 == 0)
            {
                Ach.push_back(std::complex<double>(data[2 * k + index], data[2 * k + 1 + index]));
            }
            else
            {
                Bch.push_back(std::complex<double>(data[2 * k + index], data[2 * k + 1 + index]));
            }
        }
        index = index + (step * 2);
    }

    index = 0;
    for(int i=0; i<4; i++)
    {
        for(int j=0; j < ddcSample; j++)
        {
            result.push_back(Bch[index] / Ach[index]);
            index++;
        }


    }

    return result;
}


void NpuDFProcess::setInterpolationFreq(float hfreq, float lfreq) {
    h_freq = hfreq;
    l_freq = lfreq;
}

std::complex<double> NpuDFProcess::complexMulReal(std::complex<double> mf, double r) {
    mf.real(mf.real() * r);
    mf.imag(mf.imag() * r);
    return mf;
}


void NpuDFProcess::directionFinding(float * antData, double * azi, double * musicSpec, int ddcSample) {
    double S[MAX_SVD];
    dcplx U[MAX_SVD][MAX_SVD];
    dcplx V[MAX_SVD][MAX_SVD];
    dcplx Cov[MAX_SVD][MAX_SVD];

    dcplx ant[ddcSample * 4];
    dcplx mf[361][5];

    std::vector<std::complex<double>> antNorm;
    if(test)
    {
        bool norm = true;
        if (norm)
        {
            antNorm = TestAntDF;
        }
        else
        {
            std::vector<std::complex<double>> ach;
            std::vector<std::complex<double>> bch;
            std::vector<std::complex<double>> test;

            int index = 0;
            for(int i=0; i<8; i++)
            {
                for(int j=0; j<1024; j++)
                {
                    if(i % 2 == 0)
                    {
                        ach.push_back(TestAntRawDF[index]);
                    }
                    else
                    {
                        bch.push_back(TestAntRawDF[index]);
                    }
                    index++;
                }
            }

            for(int i=0; i<ach.size(); i++)
            {
                test.push_back(bch[i] / ach[i]);
            }

            antNorm = test;
        }
    }
    else
    {
        antNorm = antNormalize(antData, ddcSample);
    }

    // convert dcplx

    for(int jdx=0; jdx<antNorm.size(); jdx++)
    {
        ant[jdx].im = antNorm[jdx].imag();
        ant[jdx].re = antNorm[jdx].real();
    }

    for(int idx=0; idx<361; idx++)
    {
        for(int jdx=0; jdx<5; jdx++)
        {
            mf[idx][jdx].im = streeVector[idx][jdx].imag();
            mf[idx][jdx].re = streeVector[idx][jdx].real();
        }
    }

    int i,j;
    for(j=0; j<MAX_SVD; j++)
    {
        for(i=0; i<MAX_SVD; i++)
        {
            U[i][j].re = 0.0;
            U[i][j].im = 0.0;
            V[i][j].re = 0.0;
            V[i][j].im = 0.0;
            Cov[i][j].re = 0.0;
            Cov[i][j].im = 0.0;
        }
        S[j] = 0.0;
        U[j][j].re = 1.0;
        U[j][j].im = 0.0;
        V[j][j].re = 1.0;
        V[j][j].im = 0.0;
        Cov[j][j].re = 1.0;
        Cov[j][j].im = 0.0;
    }

    getCovMatrix(ant, ddcSample, 0, Cov);
    getSVD(Cov, S, U, V);
    getMUSIC(mf, U, azi, musicSpec, 0);
    int debug = 0;
}

void NpuDFProcess::jacobi_sym(double M[][2*MAX_SVD], double Z[][2*MAX_SVD],
                       double d[2*MAX_SVD], int n2)
{
    for (int i=0;i<n2;++i){
        for (int j=0;j<n2;++j) Z[i][j] = (i==j)?1.0:0.0;
    }
    const int MAX_SWEEP = 60;
    for (int sweep=0; sweep<MAX_SWEEP; ++sweep){
        double off=0.0;
        for (int p=0;p<n2;++p) for (int q=p+1;q<n2;++q) off += std::fabs(M[p][q]);
        if (off < 1e-12) break;

        for (int p=0;p<n2-1;++p){
            for (int q=p+1;q<n2;++q){
                double mpq=M[p][q];
                if (std::fabs(mpq) < 1e-15) continue;
                double mpp=M[p][p], mqq=M[q][q];
                double tau=(mqq-mpp)/(2.0*mpq);
                double t=(tau>=0.0)? 1.0/(tau+std::sqrt(1.0+tau*tau))
                                        : -1.0/(-tau+std::sqrt(1.0+tau*tau));
                double c=1.0/std::sqrt(1.0+t*t);
                double s=t*c;

                for (int k=0;k<n2;++k){
                    double mkp=M[k][p], mkq=M[k][q];
                    M[k][p]=c*mkp - s*mkq;
                    M[k][q]=s*mkp + c*mkq;
                }
                for (int k=0;k<n2;++k){
                    double mpk=M[p][k], mqk=M[q][k];
                    M[p][k]=c*mpk - s*mqk;
                    M[q][k]=s*mpk + c*mqk;
                }
                M[p][q]=M[q][p]=0.0;

                for (int k=0;k<n2;++k){
                    double zkp=Z[k][p], zkq=Z[k][q];
                    Z[k][p]=c*zkp - s*zkq;
                    Z[k][q]=s*zkp + c*zkq;
                }
            }
        }
    }
    for (int i=0;i<n2;++i) d[i]=M[i][i];
}

void NpuDFProcess::getCovMatrix(const dcplx* FIFO_DF, int N, int interleavedFlag,
                            dcplx (*R)[MAX_SVD])
{
    const int USED_CH = MAX_SVD - 1; // 실제 데이터 채널 수 (4)

    // 초기화
    for (int i = 0; i < MAX_SVD; ++i) {
        for (int j = 0; j < MAX_SVD; ++j) {
            R[i][j].re = 0.0;
            R[i][j].im = 0.0;
        }
    }

    for (int n = 0; n < N; ++n)
    {
        // 4채널 샘플 로드
        double xr[USED_CH], xi[USED_CH];
        if (interleavedFlag) {
            for (int ch = 0; ch < USED_CH; ++ch) {
                int idx = n*USED_CH + ch;
                xr[ch] = FIFO_DF[idx].re;
                xi[ch] = FIFO_DF[idx].im;
            }
        } else {
            for (int ch = 0; ch < USED_CH; ++ch) {
                int idx = ch*N + n;
                xr[ch] = FIFO_DF[idx].re;
                xi[ch] = FIFO_DF[idx].im;
            }
        }

        // (A) 기준 채널(=상수 1) 포함
        R[0][0].re += 1.0;
        for (int k = 0; k < USED_CH; ++k) {
            // R[0,k+1] = Σ 1 * conj(xk) = Σ conj(xk)
            R[0][k+1].re +=  xr[k];
            R[0][k+1].im += -xi[k];

            // R[k+1,0] = Σ xk * conj(1) = Σ xk
            R[k+1][0].re +=  xr[k];
            R[k+1][0].im +=  xi[k];
        }

        // (B) 4×4 블록: R[i+1,j+1] = Σ xi * conj(xj)
        for (int i = 0; i < USED_CH; ++i) {
            double ai = xr[i];
            double bi = xi[i];
            for (int j = 0; j < USED_CH; ++j) {
                double cj = xr[j];      // conj(xj).re
                double dj = -xi[j];     // conj(xj).im
                R[i+1][j+1].re += (ai*cj - bi*dj);
                R[i+1][j+1].im += (ai*dj + bi*cj);
            }
        }
    }
}

void NpuDFProcess::getSVD(const dcplx R_in[MAX_SVD][MAX_SVD],
                   double S[MAX_SVD],
                   dcplx  U[MAX_SVD][MAX_SVD],
                   dcplx  V[MAX_SVD][MAX_SVD])
{
    const int N=MAX_SVD, N2=2*MAX_SVD;

    // 1) 2N 승격: M = [A -B; B A]
    double M[2*MAX_SVD][2*MAX_SVD], Z[2*MAX_SVD][2*MAX_SVD], d[2*MAX_SVD];
    for (int i=0;i<N2;++i) for (int j=0;j<N2;++j) M[i][j]=0.0;

    for (int i=0;i<N;++i){
        for (int j=0;j<N;++j){
            double A=R_in[i][j].re;
            double B=R_in[i][j].im;
            M[i][j]     = A;
            M[i][j+N]   = -B;
            M[i+N][j]   =  B;
            M[i+N][j+N] =  A;
        }
    }

    // 2) 실대칭 Jacobi
    jacobi_sym(M, Z, d, N2);

    // 3) 내림차순 정렬
    int idx[2*MAX_SVD];
    for (int i=0;i<N2;++i) idx[i]=i;
    for (int a=0;a<N2-1;++a){
        int m=a;
        for (int b=a+1;b<N2;++b) if (d[b]>d[m]) m=b;
        if (m!=a){ double td=d[a]; d[a]=d[m]; d[m]=td; int ti=idx[a]; idx[a]=idx[m]; idx[m]=ti; }
    }

    // 4) 중복 쌍 제거하며 상위 N개 복원
    const double DUP_TOL = 1e-9;
    int kept=0; double last=0.0; int has_last=0;

    for (int t=0; t<N2 && kept<N; ++t){
        int col = idx[t];
        double lam = d[t];

        if (has_last){
            double rel = std::fabs(lam - last)/ (std::fabs(lam)>1.0? std::fabs(lam):1.0);
            if (rel < DUP_TOL) continue; // 중복 쌍 skip
        }

        double nr=0.0;
        for (int i=0;i<N;++i){
            double u = Z[i][col];
            double v = Z[i+N][col];
            U[i][kept].re = u; U[i][kept].im = v;
            nr += u*u + v*v;
        }
        nr = (nr>0.0)? std::sqrt(nr) : 1.0;
        for (int i=0;i<N;++i){ U[i][kept].re/=nr; U[i][kept].im/=nr; }
        S[kept] = lam;

        last=lam; has_last=1; kept++;
    }

    // 5) V = U
    for (int i=0;i<N;++i) for (int k=0;k<N;++k) V[i][k]=U[i][k];
}

void NpuDFProcess::getMUSIC(const dcplx(*Manifold)[MAX_SVD],
                     const dcplx U[MAX_SVD][MAX_SVD],
                     double *dEst_Azi,          /* out[0..K-1] */
                     double *dMUSIC_Spec,       /* coarse 361개 (optional) */
                     int sig_num)
{
    const int N = MAX_SVD;
    int i, j, k;

    /* K 범위 강제: 1..N-1 */
    if (sig_num < 1)   sig_num = 1;
    if (sig_num > N - 1) sig_num = N - 1;

    /* Q = En En^H */
    dcplx Q[MAX_SVD][MAX_SVD];
    for (i = 0; i<N; ++i) for (j = 0; j<N; ++j){ Q[i][j].re = 0.0; Q[i][j].im = 0.0; }
    for (k = sig_num; k<N; ++k){
        for (i = 0; i<N; ++i){
            for (j = 0; j<N; ++j){
                double ar = U[i][k].re, ai = U[i][k].im;
                double br = U[j][k].re, bi = -U[j][k].im; /* conj */
                Q[i][j].re += ar*br - ai*bi;
                Q[i][j].im += ar*bi + ai*br;
            }
        }
    }

    /* 1) coarse: 0..360, 1° */
    {
        double Pcoarse[361];
        double maxPc = -1.0;
        dcplx y[MAX_SVD];

        for (i = 0; i <= 360; ++i){
            const dcplx* a = Manifold[i];
            matv_mul_Qa_c(Q, a, y, N);
            {
                dcplx den_c = c_dot_h_c(a, y, N);
                double den = den_c.re; if (den <= 1e-18) den = 1e-18;
                Pcoarse[i] = 1.0 / den;
                if (dMUSIC_Spec) dMUSIC_Spec[i] = Pcoarse[i];
                if (Pcoarse[i] > maxPc) maxPc = Pcoarse[i];
            }
        }
        if (maxPc <= 0.0) maxPc = 1.0;

        /* 2) seeds: 넉넉히 (3*K, 상한 20), suppression 적용 */
        {
            int desiredSeeds = MUSIC_SEED_MULTIPLIER*sig_num;
            if (desiredSeeds > MUSIC_SEED_CAP) desiredSeeds = MUSIC_SEED_CAP;

            int used[361];
            int seeds[MAX_SEEDS];
            int seed_count = 0;
            const int suppress = (int)((MUSIC_SUPPRESS_DEG >= 1.0) ? floor(MUSIC_SUPPRESS_DEG + 0.5) : 1);

            for (i = 0; i <= 360; ++i) used[i] = 0;

            for (k = 0; k<desiredSeeds; ++k){
                double bestP = -1.0; int bestI = -1;
                for (i = 0; i <= 360; ++i){
                    if (used[i]) continue;
                    if (Pcoarse[i] > bestP){ bestP = Pcoarse[i]; bestI = i; }
                }
                if (bestI < 0) break;
                seeds[seed_count++] = bestI;

                /* 억제 */
                for (j = -suppress; j <= suppress; ++j){
                    int ii = bestI + j;
                    while (ii <   0) ii += 361;
                    while (ii > 360) ii -= 361;
                    used[ii] = 1;
                }
                if (seed_count >= MAX_SEEDS) break;
            }

            /* 3) fine 후보 수집 */
            PeakCand_c cands[MAX_CANDS];
            int        cands_n = 0;

            for (i = 0; i<seed_count; ++i){
                int center = seeds[i];
                PeakCand_c tmp[MAX_CANDS];
                int got = CollectFineCandidates_c(Q, Manifold, center,
                                                  MUSIC_FINE_WINDOW_DEG,
                                                  MUSIC_FINE_STEP_DEG,
                                                  tmp, MAX_CANDS);
                /* 붙여넣기 */
                int t;
                for (t = 0; t<got && cands_n<MAX_CANDS; ++t){
                    cands[cands_n++] = tmp[t];
                }
            }

            /* coarse 가중치 score = P_fine * (Pcoarse(seed)/maxPc)^alpha */
            {
                double alpha = MUSIC_COARSE_WEIGHT_ALPHA;
                for (i = 0; i<cands_n; ++i){
                    double cw = Pcoarse[cands[i].seed_deg] / maxPc;
                    if (cw < 1e-6) cw = 1e-6;
                    /* pow from <math.h> */
                    cands[i].score = cands[i].P * pow(cw, alpha);
                }
            }

            /* 4) 전역 정렬 + per-window cap + NMS */
            sort_candidates_by_score_desc_c(cands, cands_n);

            /* per-window cap = ceil(K/2) */
            int per_window_cap = (sig_num <= 2) ? sig_num : ((sig_num + 1) / 2);
            /* seed별 카운트: deg(0..360) 인덱스로 사용 */
            int taken_per_seed[361];
            for (i = 0; i <= 360; ++i) taken_per_seed[i] = 0;

            /* 최종 선택 */
            {
                int outCount = 0;
                for (i = 0; i<cands_n && outCount<sig_num; ++i){
                    int sd = cands[i].seed_deg;
                    if (taken_per_seed[sd] >= per_window_cap) continue;

                    /* 전역 최소 간격(NMS) */
                    {
                        int clash = 0, m;
                        for (m = 0; m<outCount; ++m){
                            if (too_close_deg_c(cands[i].deg, dEst_Azi[m], MUSIC_MIN_SEPARATION_DEG)){
                                clash = 1; break;
                            }
                        }
                        if (clash) continue;
                    }

                    dEst_Azi[outCount++] = cands[i].deg;
                    taken_per_seed[sd]++;

                    if (outCount >= sig_num) break;
                }
                /* 부족분 0으로 채움 */
                while (outCount < sig_num) dEst_Azi[outCount++] = 0.0;
            }
        } /* seeds scope */
    } /* coarse scope */
}

void NpuDFProcess::matv_mul_Qa_c(const dcplx Q[MAX_SVD][MAX_SVD],
                          const dcplx* a, dcplx* y, int n)
{
    int i, j;
    for (i = 0; i<n; ++i){
        double rr = 0.0, ii = 0.0;
        for (j = 0; j<n; ++j){
            rr += Q[i][j].re*a[j].re - Q[i][j].im*a[j].im;
            ii += Q[i][j].re*a[j].im + Q[i][j].im*a[j].re;
        }
        y[i].re = rr; y[i].im = ii;
    }
}


dcplx NpuDFProcess::c_dot_h_c(const dcplx* a, const dcplx* b, int n){
    int i;
    double rr = 0.0, ii = 0.0;
    for (i = 0; i<n; ++i){
        double cr = a[i].re, ci = -a[i].im; /* conj(a) */
        double xr = b[i].re, xi = b[i].im;
        rr += cr*xr - ci*xi;
        ii += cr*xi + ci*xr;
    }
    { dcplx z; z.re = rr; z.im = ii; return z; }
}

int NpuDFProcess::too_close_deg_c(double a, double b, double sep){
    double d = fabs(a - b);
    if (d > 180.0) d = 360.0 - d;
    return (d < sep);
}

/* fine 창에서 국소 최대 후보 수집 (배열 기반) */
int NpuDFProcess::CollectFineCandidates_c(const dcplx Q[MAX_SVD][MAX_SVD],
                                   const dcplx(*Manifold)[MAX_SVD],
                                   int center_deg,
                                   double halfW, double step,
                                   PeakCand_c* out /*[MAX_CANDS]*/,
                                   int out_cap)
{
    /* 그리드 버퍼 (고정 길이) */
    double gdeg[MAX_FINE_STEPS];
    double gP[MAX_FINE_STEPS];
    int    steps, s, cand_count = 0;

    steps = (int)floor((2.0*halfW) / step + 0.5) + 1;
    if (steps < 3) steps = 3;
    if (steps > MAX_FINE_STEPS) steps = MAX_FINE_STEPS;

    for (s = 0; s<steps; ++s){
        double deg = center_deg - halfW + s*step;
        while (deg <  0.0)  deg += 360.0;
        while (deg >= 360.0) deg -= 360.0;

        {
            int idx0 = (int)floor(deg);
            double t = deg - idx0;
            dcplx a[MAX_SVD], y[MAX_SVD];
            BuildA_FromLUT_PhaseInterp_c(Manifold, idx0, t, a);

            matv_mul_Qa_c(Q, a, y, MAX_SVD);
            {
                dcplx den_c = c_dot_h_c(a, y, MAX_SVD);
                double den = den_c.re;
                if (den <= 1e-18) den = 1e-18;
                gdeg[s] = deg;
                gP[s] = 1.0 / den;
            }
        }
    }

    /* 국소 최대만 out[]에 적재 */
    for (s = 0; s<steps; ++s){
        double Pi = gP[s];
        int isMax = 1;
        if (s>0 && gP[s - 1] > Pi) isMax = 0;
        if (s + 1<steps && gP[s + 1] > Pi) isMax = 0;
        if (isMax){
            if (cand_count < out_cap){
                out[cand_count].deg = gdeg[s];
                out[cand_count].P = Pi;
                out[cand_count].seed_deg = center_deg;
                out[cand_count].score = Pi; /* 가중치는 나중에 부여 */
                ++cand_count;
            }
        }
    }
    return cand_count;
}

/* 점수 기준 내림차순 선택정렬 (STL sort 대체) */
void NpuDFProcess::sort_candidates_by_score_desc_c(PeakCand_c* a, int n){
    int i, j, m;
    for (i = 0; i<n - 1; ++i){
        m = i;
        for (j = i + 1; j<n; ++j) if (a[j].score > a[m].score) m = j;
        if (m != i){
            PeakCand_c tmp = a[i]; a[i] = a[m]; a[m] = tmp;
        }
    }
}

void NpuDFProcess::BuildA_FromLUT_PhaseInterp_c(const dcplx(*Manifold)[MAX_SVD], int idx0, double t, dcplx* out /*[MAX_SVD]*/)
{
    const dcplx* a0 = Manifold[idx0 % 361];
    const dcplx* a1 = Manifold[(idx0 + 1) % 361];
    int ch;
    out[0].re = 1.0; out[0].im = 0.0; /* 상수 채널 */
    for (ch = 1; ch<MAX_SVD; ++ch){
        double m0, p0, m1, p1;
        to_polar_c(a0[ch], &m0, &p0);
        to_polar_c(a1[ch], &m1, &p1);
        {
            double p1u = unwrap_to_prev_c(p0, p1);
            double p = p0 + t*(p1u - p0);
            double m = m0 + t*(m1 - m0); /* LUT가 unit이면 m=1로 고정 가능 */
            out[ch].re = m*cos(p);
            out[ch].im = m*sin(p);
        }
    }
}

void NpuDFProcess::to_polar_c(dcplx z, double* mag, double* ph){
    *mag = sqrt(z.re*z.re + z.im*z.im);
    *ph = atan2(z.im, z.re);
}

double NpuDFProcess::unwrap_to_prev_c(double prev, double curr){
    double d = curr - prev;
    while (d >  PI) d -= 2.0*PI;
    while (d < -PI) d += 2.0*PI;
    return prev + d;
}

/*

void onIQDataReceived(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate, const float* ddc_iq, const size_t ddc_samples);

private:
double getPowerLevel(const float* ddc_iq, size_t ddc_samples);

void initFile();
void writeToFile(const float* ddc_iq, const int samples);
void closeFile();
void initFileSaveInfo(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate);
void updateFileSaveInfo(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate);
bool saveDatabase();
*/

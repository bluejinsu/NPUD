#include "FileReader.h"

FileReader::FileReader() {

}

FileReader::~FileReader() {
    Close();
}

bool FileReader::Open(std::string path) {
    _file.open(path, std::ios::binary | std::ios::in);
    return _file.is_open();
}

int FileReader::FileSize() {
    if(!_file.is_open())
        return 0;

    auto cur = _file.tellg();
    _file.seekg(0, std::ios::end);
    auto len = _file.tellg();
    _file.seekg(cur);
    return static_cast<int>(len);
}

int FileReader::Read(void *dst, int size) {
    if(!_file.is_open())
        return 0;

    _file.read(reinterpret_cast<char*>(dst), size);
    return _file.gcount();
}

void FileReader::Close() {
    if(_file.is_open())
        _file.close();
};

//std::vector<FileInfo> FileReader::scanCalDir(std::filesystem::path dir) {
//    std::vector<FileInfo> fileList;
//
//    for(auto &e : std::filesystem::directory_iterator(dir)) {
//        FileInfo info;
//
//        if(parseCalFileName(e.path(), info))
//            fileList.push_back(info);
//    }
//}


bool FileReader::parseCalFileName(const std::string path, FileInfo& out, int64_t freq) {
    static const std::regex rx(R"(^([0-9]+(?:\.[0-9]+)?)_([0-9]{8})_([0-9]{6})\.bin$)",
                               std::regex::ECMAScript | std::regex::icase);


    const std::string fname = path;
    std::smatch m;
    if (!std::regex_match(fname, m, rx)) return false;

    // 주파수(Hz, 실수 허용)
    try { out.freq = std::stoull(m[1].str()); }
    catch (...) { return false; }

    if((freq - 15e6) <= out.freq && (freq + 15e6) >= out.freq)
    {
        // YYYYMMDD + HHMMSS → epoch(로컬)
        const std::string ymd = m[2].str();
        const std::string hms = m[3].str();

        std::tm t{};
        t.tm_year = std::stoi(ymd.substr(0,4));
        t.tm_mon  = std::stoi(ymd.substr(4,2));
        t.tm_mday = std::stoi(ymd.substr(6,2));
        t.tm_hour = std::stoi(hms.substr(0,2));
        t.tm_min  = std::stoi(hms.substr(2,2));
        t.tm_sec  = std::stoi(hms.substr(4,2));
        t.tm_isdst = -1;

        std::time_t epoch = std::mktime(&t);
        if (epoch == -1) return false;

        out.epoch = epoch;
        out.path = path;
        out.file_size = 0;
        return true;
    }
    else
    {
        return false;
    }
}
























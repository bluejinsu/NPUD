#ifndef FILEREADER_H
#define FILEREADER_H

#include <fstream>
#include <iostream>
#include <filesystem>
#include <regex>

struct FileInfo {
    std::string path;
    int64_t freq;
    std::time_t epoch = 0;
    std::uintmax_t file_size;
};

class FileReader {
private:
    std::ifstream _file;

public:
    FileReader();
    ~FileReader();

    bool Open(std::string path);
    int FileSize();

    int Read(void *dst, int size);

    void Close();

    std::vector<FileInfo> scanCalDir(std::filesystem::path dir);
    bool parseCalFileName(const std::string path, FileInfo& out, int64_t freq);

};

#endif // FILEREADER_H

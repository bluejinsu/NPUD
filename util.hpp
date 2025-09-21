#ifndef UTIL_H
#define UTIL_H

#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include <time.h>

inline std::string generateID() {
    static std::mutex id_mutex;
    static int counter = 0;

    std::lock_guard<std::mutex> lock(id_mutex);

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y%m%d%H%M%S")
        << std::setw(3) << std::setfill('0') << milliseconds.count();

    counter = (counter + 1) % 1000;
    ss << std::setw(3) << std::setfill('0') << counter;

    return ss.str();
}

inline std::tm stringToTm(const std::string& dateTimeStr) {
    std::tm tm = {};
    std::istringstream ss(dateTimeStr);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

    if (ss.fail()) {
        throw std::runtime_error("Failed to parse time string.");
    }
    return tm;
}

inline time_t localToUtcTimestamp(const std::string& localTimeStr) {
    std::tm tm = stringToTm(localTimeStr);

    time_t localTime = std::mktime(&tm);
    if (localTime == -1) {
        throw std::runtime_error("Failed to convert to time_t.");
    }

    time_t utcTime = localTime - 0;

    return utcTime;
}

inline std::string timestampToString(time_t in_time_t) {
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

inline std::string convertPathToWindowsFormat(const std::string& unixPath) {
    const std::string unixRoot = "/data/";
    const std::string windowsRoot = "Z:\\";

    if (unixPath.find(unixRoot) != 0) {
        return unixPath;
    }

    std::string windowsPath = windowsRoot + unixPath.substr(unixRoot.size());
    std::replace(windowsPath.begin(), windowsPath.end(), '/', '\\');

    return windowsPath;
}

#endif
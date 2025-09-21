#ifndef IF_DATA_FORMAT_ENCODER_H
#define IF_DATA_FORMAT_ENCODER_H

#include <boost/shared_ptr.hpp>

#include <inttypes.h>
#include <vector>

class IFDataFormatEncoder
{
private:
    uint32_t m_frameCount;
    uint32_t m_blockCount;
    uint32_t m_samplerate;
    uint64_t m_frequency;
    uint32_t m_bandwidth;

    uint32_t m_frameType;
    uint32_t m_recipGain;
    int32_t m_antennaVoltageRef;

    uint64_t m_bigtimeTimeStamp;

public:
    IFDataFormatEncoder(uint32_t samplerate, uint64_t frequency, uint32_t bandwidth, time_t startTime = 0);

    virtual boost::shared_ptr<std::vector<char>> encodeIQData(const void* pData, size_t bytes);

    void setBlockCount(uint32_t blockCount);
    void setFrameType(uint32_t type);
    void setRecipGain(uint32_t recipGain);
    void setAntennaVoltageRef(int32_t antennaVoltageRef);
    uint32_t getBlockCount();
    uint32_t getFrameType();
    uint32_t getRecipGain();
    int32_t getAntennaVoltageRef();
};

#endif
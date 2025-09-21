#include "IFDataFormatEncoder.h"

#define WINVER 0x0501

#include "R&S/rs_gx40x_global_frame_header_if_defs.h"
#include "R&S/rs_gx40x_global_ifdata_header_if_defs.h"

#include <boost/make_shared.hpp>

#include <cstring>
#include <vector>


IFDataFormatEncoder::IFDataFormatEncoder(uint32_t samplerate, uint64_t frequency, uint32_t bandwidth, time_t startTime)
    : m_frameCount(0)
    , m_blockCount(16)
    , m_samplerate(samplerate)
    , m_frequency(frequency)
    , m_bandwidth(bandwidth)
    , m_frameType(5)
    , m_recipGain(0x22200000)
    , m_antennaVoltageRef(360)
    , m_bigtimeTimeStamp(startTime * 1000000)
{

}

boost::shared_ptr<std::vector<char>> IFDataFormatEncoder::encodeIQData(const void* pData, size_t bytes)
{
    size_t frameSize = sizeof(typFRH_FRAMEHEADER) + sizeof(typIFD_IFDATAHEADER) + m_blockCount * sizeof(uint32_t) + bytes;

    auto pIFFrameData = boost::make_shared<std::vector<char>>();
    pIFFrameData->resize(frameSize);

    typFRH_FRAMEHEADER frameHeader;
    memset(&frameHeader, 0x00, sizeof(typFRH_FRAMEHEADER));

    frameHeader.uintMagicWord = 0xfb746572;
    frameHeader.uintFrameLength = static_cast<uint32_t>(frameSize / sizeof(uint32_t));
    frameHeader.uintFrameCount = m_frameCount;
    frameHeader.uintFrameType = m_frameType;
    frameHeader.uintDataHeaderLength = sizeof(typIFD_IFDATAHEADER) / sizeof(uint32_t);
    frameHeader.uintStatusword = 0;

    memcpy(&(*pIFFrameData)[0], &frameHeader, sizeof(typFRH_FRAMEHEADER));

    typIFD_IFDATAHEADER ifdataHeader;
    memset(&ifdataHeader, 0x00, sizeof(ifdataHeader));

    ifdataHeader.uintDatablockCount = m_blockCount; // 0x10
    ifdataHeader.uintDatablockLength = static_cast<uint32_t>(bytes / sizeof(uint32_t) / m_blockCount); // 0x400

    m_bigtimeTimeStamp += static_cast<uint64_t>(((bytes / sizeof(uint32_t) / 2 / (double)m_samplerate)) * 1000000ll);
    ifdataHeader.bigtimeTimeStamp.uint64TimeInOneWord
        = m_bigtimeTimeStamp;

    ifdataHeader.uintStatusword = 0;
    ifdataHeader.uintTunerFrequency_Low = m_frequency & 0xFFFFFFFF;
    ifdataHeader.uintTunerFrequency_High = static_cast<uint32_t>(m_frequency & 0xFFFFFFFF00000000);
    ifdataHeader.uintBandwidth = m_bandwidth;
    ifdataHeader.uintSamplerate = m_samplerate;
    ifdataHeader.uintInterpolation = 0x1;
    ifdataHeader.uintDecimation = 1;
    ifdataHeader.intAntennaVoltageRef = m_antennaVoltageRef;

    memcpy(&(*pIFFrameData)[sizeof(typFRH_FRAMEHEADER)], &ifdataHeader, sizeof(typIFD_IFDATAHEADER));

    for (uint32_t k = 0; k < m_blockCount; k++)
    {
        auto pFrame = &(*pIFFrameData)[0];
        auto headerOffset = sizeof(typFRH_FRAMEHEADER) + sizeof(typIFD_IFDATAHEADER);
        auto blockOffset = k * (sizeof(uint32_t) + ifdataHeader.uintDatablockLength * sizeof(uint32_t));

        auto pDataBlock = reinterpret_cast<uint32_t*>(&pFrame[headerOffset + blockOffset]);
        pDataBlock[0] = m_recipGain;

        // copy block data
        memcpy(&pDataBlock[1], &((const char*)pData)[k * ifdataHeader.uintDatablockLength * sizeof(uint32_t)], ifdataHeader.uintDatablockLength * sizeof(uint32_t));
    }

    m_frameCount++;

    return pIFFrameData;
}

void IFDataFormatEncoder::setBlockCount(uint32_t blockCount)
{
    m_blockCount = blockCount;
}

void IFDataFormatEncoder::setFrameType(uint32_t frameType)
{
    m_frameType = frameType;
}

void IFDataFormatEncoder::setRecipGain(uint32_t recipGain)
{
    m_recipGain = (recipGain << 16);
}

void IFDataFormatEncoder::setAntennaVoltageRef(int32_t antennaVoltageRef)
{
    m_antennaVoltageRef = antennaVoltageRef;
}

uint32_t IFDataFormatEncoder::getBlockCount()
{
    return m_blockCount;
}

uint32_t IFDataFormatEncoder::getFrameType()
{
    return m_frameType;
}

uint32_t IFDataFormatEncoder::getRecipGain()
{
    uint32_t l_recipGain = m_recipGain >> 16;
    return l_recipGain;
}

int32_t IFDataFormatEncoder::getAntennaVoltageRef()
{
    return m_antennaVoltageRef;
}

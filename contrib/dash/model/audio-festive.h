/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#ifndef AUDIO_FESTIVE_H
#define AUDIO_FESTIVE_H

#include <vector>

#include "tcp-stream-adaptation-algorithm.h"

namespace ns3 {

/**
 * \brief 专为音频设计的简化版 ABR 算法
 * 特点：更大的缓冲区目标，更简单的切换逻辑，去除了复杂的公平性随机化。
 */
class AudioFestiveAlgorithm : public AdaptationAlgorithm {
 public:
  AudioFestiveAlgorithm(
      const videoData &videoData,  // 注意：实际上存的是音频码率信息
      const playbackData &playbackData, const bufferData &bufferData,
      const throughputData &throughput);

  algorithmReply GetNextRep(const int64_t segmentCounter, int64_t clientId);

 private:
  const int64_t m_targetBuf;        // 目标缓冲区 (微秒)
  const double m_thrptThrsh;        // 带宽使用阈值
  const int64_t m_highestRepIndex;  // 最高音质索引
  int m_stableCounter;              // 简化的稳定计数器
};

}  // namespace ns3
#endif /* AUDIO_FESTIVE_H */
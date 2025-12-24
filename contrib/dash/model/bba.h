#ifndef BBA_ALGORITHM_H
#define BBA_ALGORITHM_H

#include "tcp-stream-adaptation-algorithm.h"

namespace ns3 {

class BbaAlgorithm : public AdaptationAlgorithm {
 public:
  BbaAlgorithm(const videoData &videoData, const playbackData &playbackData,
               const bufferData &bufferData, const throughputData &throughput);

  virtual algorithmReply
  GetNextRep(const int64_t segmentCounter, int64_t clientId);

 private:
  // ===== BBA 核心参数 =====
  double m_reservoir;  // buffer 下限（秒）
  double m_cushion;    // buffer 映射区间（秒）
  int m_highestRepIndex;
};

}  // namespace ns3

#endif  // BBA_ALGORITHM_H

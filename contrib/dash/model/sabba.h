#ifndef SABBA_ALGORITHM_H
#define SABBA_ALGORITHM_H
#include "tcp-stream-adaptation-algorithm.h"

namespace ns3 {

class SabbaAlgorithm : public AdaptationAlgorithm {
 public:
  SabbaAlgorithm(const videoData &videoData, const playbackData &playbackData,
                 const bufferData &selfbufferData,
                 const bufferData &peerbufferData,
                 const throughputData &selfthroughput,
                 const throughputData &peerthroughput);

  virtual algorithmReply
  GetNextRep(const int64_t segmentCounter, int64_t clientId) override;
  int64_t GetSelfBufferUs() const;
  int64_t GetPeerBufferUs() const;
  int StepDownRep(int rep, int steps) const;

 private:
  const double m_reservoir;   // buffer 下限（秒）
  const double m_cushion;     // buffer 映射区间（秒）
  const int64_t m_targetBuf;  // buffer 最大值
  const int64_t m_delta;
  const bufferData &m_peerbufferData;
  const throughputData &m_peerthroughput;
  const int m_highestRepIndex;  // 最大码率

  // 0 代表没有进入共享瓶颈
  // 1 代表在共享瓶颈里面是弱势方，
  // 2 初步降低码率
  // 3 代表降低码率没有用，需要停止请求
  int inSBstate = 0;

 private:
  algorithmReply
  GetNextRepStandalone(const int64_t segmentCounter, int64_t clientId);

  int64_t GetSelfBufferedData() const;
  int64_t GetPeerBufferedData() const;

  double ComputeWeightedThroughputMbps(const throughputData &data,
                                       size_t recentCount, double alpha);

  // 阈值统一用“时间”表示，单位微秒
  const int64_t m_lowBufferThresholdUs;
  const int64_t m_highBufferThresholdUs;
  const int64_t m_veryHighBufferThresholdUs;
  const int64_t m_safetyMargin;

  int m_consecutiveYieldCount = 0;           // 连续让步但未见效的次数
  const int m_yieldEscalationThreshold = 3;  // 达到该次数后直接休眠
};

}  // namespace ns3

#endif

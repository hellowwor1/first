#ifndef ABRV2_ALGORITHM_H
#define ABRV2_ALGORITHM_H

#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>
#include <vector>

#include "tcp-stream-adaptation-algorithm.h"

namespace ns3 {

class Abrv2Algorithm : public AdaptationAlgorithm {
 public:
  Abrv2Algorithm(const videoData &selfvideoData, const videoData &peervideoData,
                 const playbackData &selfplaybackData,
                 const playbackData &peerplaybackData,
                 const bufferData &selfbufferData,
                 const bufferData &peerbufferData,
                 const throughputData &selfthroughput,
                 const throughputData &peerthroughput);

  virtual algorithmReply
  GetNextRep(const int64_t segmentCounter, int64_t clientId) override;

 private:
  // ===== 基础状态 =====
  int64_t GetSelfBufferUs() const;
  int64_t GetPeerBufferUs() const;
  int64_t GetSelfBufferedData() const;
  int64_t GetPeerBufferedData() const;

  double ComputeWeightedThroughputMbps(const throughputData &data,
                                       size_t recentCount = 10,
                                       double alpha = 0.7) const;

  int StepDownRep(int rep, int steps) const;

  // ===== 码率 / 大小 / 码率历史 =====
  double GetSelfSegmentSizeMbits(int rep, int64_t segmentCounter) const;
  double GetPeerSegmentSizeMbits(int rep, int64_t segmentCounter) const;

  // ===== 风险度量化 =====
  double BufferDangerPressure(int64_t bufferUs) const;
  double CalcSensitivity(int rep, int64_t segmentCounter, double throughputMbps,
                         int64_t bufferUs) const;

  // ===== 基础码率选择 =====
  double QualityValue(int rep) const;
  double RebufferPenalty(int rep, int64_t segmentCounter, double throughputMbps,
                         int64_t bufferUs) const;
  double ImbalancePenalty(int rep) const;
  double SwitchPenalty(int rep) const;
  double
  SensitivityPenalty(int rep, int64_t segmentCounter, double throughputMbps,
                     int64_t bufferUs, int64_t bufferedDataBits) const;

  double BaseRepScore(int rep, int64_t segmentCounter, double selfTpMbps,
                      int64_t selfBufferUs, int64_t selfBufferedBits) const;

  int SelectBaseRep(int64_t segmentCounter, double selfTpMbps,
                    int64_t selfBufferUs, int64_t selfBufferedBits) const;

  // ===== peer危险判断 =====
  bool IsSharedBottleneck() const;
  bool IsPeerDangerous(int peerRep, int64_t segmentCounter, double peerTpMbps,
                       int64_t peerBufferUs) const;
  // 本方是否富裕
  bool IsSelfRich(int selfRep, int64_t segmentCounter, double selfTpMbps,
                  int64_t selfBufferUs) const;

  // ===== 协同码率选择 =====
  double SelfSacrificeCost(int coopRep, int baseRep) const;
  double
  PeerBenefit(int coopRep, int baseRep, int peerRep, int64_t segmentCounter,
              double peerTpMbps, int64_t peerBufferUs) const;

  int SelectCoopRep(int baseRep, int peerRep, int64_t segmentCounter,
                    double peerTpMbps, int64_t peerBufferUs) const;

  // ===== 停等决策 =====
  bool HasPauseAbility(int coopRep, int64_t selfBufferUs, double selfTpMbps,
                       int64_t segmentCounter) const;

  double ComputeBackgroundDiscount() const;

  int64_t
  ComputePauseDurationUs(int coopRep, int peerRep, int64_t segmentCounter,
                         double selfTpMbps, double peerTpMbps,
                         int64_t selfBufferUs, int64_t peerBufferUs) const;

  algorithmReply
  GetNextRepStandalone(const int64_t segmentCounter, int64_t clientId);

 private:
  const double m_reservoir;   // buffer 下限（秒）
  const double m_cushion;     // buffer 映射区间（秒）
  const int64_t m_targetBuf;  // buffer 最大值
  int64_t m_selfsegmentCounter;
  int64_t m_peersegmentCounter;

  const int64_t m_delta;

  const videoData &m_peervideoData;
  const playbackData &m_peerplaybackData;
  const bufferData &m_peerbufferData;
  const throughputData &m_peerthroughput;

  const int m_highestRepIndex;

  const int64_t m_lowBufferThresholdUs;
  const int64_t m_highBufferThresholdUs;
  const int64_t m_veryHighBufferThresholdUs;
  const int64_t m_safetyMargin;

  // ===== 评分系数 =====
  const double m_alphaRebuffer = 4.0;
  const double m_betaImbalance = 1.0;
  const double m_gammaSwitch = 0.6;
  const double m_etaSensitivity = 1.0;

  // ===== 危险区放大 =====
  const double m_lambdaBufferDanger = 1.5;

  // ===== 协同码率 =====
  const double m_lambdaPeerBenefit = 6.0;
  const double m_lambdaSelfSacrifice = 1.0;

  // ===== peer危险阈值 =====
  const double m_peerDangerTheta = 1.0;

  // ===== 本方富裕阈值 =====
  const double m_selfRichTheta = 0.25;

  // ===== 最小有效停等 =====
  const int64_t m_minPauseUs;

  // ===== 系统最大停等 =====
  const int64_t m_maxSystemPauseUs;
};

}  // namespace ns3

#endif  // ABR_H
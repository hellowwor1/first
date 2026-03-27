#include "abrv2.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("Abrv2Algorithm");
NS_OBJECT_ENSURE_REGISTERED(Abrv2Algorithm);

Abrv2Algorithm::Abrv2Algorithm(
    const videoData &selfvideoData, const videoData &peervideoData,
    const playbackData &selfplaybackData, const playbackData &peerplaybackData,
    const bufferData &selfbufferData, const bufferData &peerbufferData,
    const throughputData &selfthroughput, const throughputData &peerthroughput)
    : AdaptationAlgorithm(selfvideoData, selfplaybackData, selfbufferData,
                          selfthroughput),
      m_reservoir(6000000.0),  // 6 秒安全缓冲
      m_cushion(20000000.0),   // 20 秒线性映射区间
      m_targetBuf(30000000),   // 30s的缓冲区水平
      m_delta(selfbufferData.m_segmentDuration),
      m_peervideoData(peervideoData),
      m_peerplaybackData(peerplaybackData),
      m_peerbufferData(peerbufferData),
      m_peerthroughput(peerthroughput),
      m_highestRepIndex(selfvideoData.averageBitrate.size() - 1),
      m_lowBufferThresholdUs(6000000),
      m_highBufferThresholdUs(9000000),
      m_veryHighBufferThresholdUs(10000000),
      m_safetyMargin(3000000),
      m_minPauseUs(selfbufferData.m_segmentDuration / 4),
      m_maxSystemPauseUs(2 * selfbufferData.m_segmentDuration) {
  NS_ASSERT_MSG(m_highestRepIndex >= 0,
                "The highest quality representation index should be >= 0");
}

int64_t Abrv2Algorithm::GetSelfBufferUs() const {
  return m_bufferData.m_segmentsInBuffer * m_bufferData.m_segmentDuration;
}

int64_t Abrv2Algorithm::GetPeerBufferUs() const {
  return m_peerbufferData.m_segmentsInBuffer *
         m_peerbufferData.m_segmentDuration;
}

int64_t Abrv2Algorithm::GetSelfBufferedData() const {
  return std::accumulate(m_bufferData.segmentSizes.begin(),
                         m_bufferData.segmentSizes.end(), (int64_t)0);
}

int64_t Abrv2Algorithm::GetPeerBufferedData() const {
  return std::accumulate(m_peerbufferData.segmentSizes.begin(),
                         m_peerbufferData.segmentSizes.end(), (int64_t)0);
}

int Abrv2Algorithm::StepDownRep(int rep, int steps) const {
  return std::max(0, rep - steps);
}

double Abrv2Algorithm::ComputeWeightedThroughputMbps(const throughputData &data,
                                                     size_t recentCount,
                                                     double alpha) const {
  const size_t n = data.bytesReceived.size();
  if (n == 0) {
    return 0.0;
  }

  size_t count = std::min(recentCount, n);

  double weightedBits = 0.0;
  double weightedUs = 0.0;
  double weight = 1.0;

  for (size_t k = 0; k < count; ++k) {
    size_t i = n - 1 - k;

    int64_t startUs = data.transmissionStart[i];
    int64_t endUs = data.transmissionEnd[i];
    int64_t bytes = data.bytesReceived[i];
    int64_t durationUs = endUs - startUs;

    if (durationUs <= 0 || bytes <= 0) {
      weight *= alpha;
      continue;
    }

    weightedBits += weight * bytes * 8.0;
    weightedUs += weight * durationUs;
    weight *= alpha;
  }

  if (weightedUs <= 0.0) {
    return 0.0;
  }

  // bit/us == Mbps
  return weightedBits / weightedUs;
}

// ===== representation 对应参数 =====

double
Abrv2Algorithm::GetSelfSegmentSizeMbits(int rep, int64_t segmentCounter) const {
  rep = std::max(0, std::min(rep, m_highestRepIndex));
  return m_videoData.segmentSize[rep][segmentCounter] * 8.0 / 1000000.0;
}

double
Abrv2Algorithm::GetPeerSegmentSizeMbits(int rep, int64_t segmentCounter) const {
  rep = std::max(0, std::min(rep, m_highestRepIndex));
  return m_peervideoData.segmentSize[rep][segmentCounter] * 8.0 / 1000000.0;
}

// ===== 风险度量化 =====

double Abrv2Algorithm::BufferDangerPressure(int64_t bufferUs) const {
  double Bref = (double)m_lowBufferThresholdUs;
  double B = (double)bufferUs;
  return std::max(0.0, (Bref - B) / std::max(1.0, Bref));
}

double
Abrv2Algorithm::CalcSensitivity(int rep, int64_t segmentCounter,
                                double throughputMbps, int64_t bufferUs) const {
  double eps = 0.01;
  double nextSizeMbits = GetSelfSegmentSizeMbits(rep, segmentCounter);

  // 下一块下载时间
  double T = nextSizeMbits / std::max(0.01, throughputMbps);

  // 时间侧buffer（秒）
  double Btime = bufferUs / 1e6;

  // 低buffer危险区放大
  double P = BufferDangerPressure(bufferUs);

  double timeRisk = T / std::max(eps, Btime);

  return timeRisk * (1.0 + m_lambdaBufferDanger * P);
}

// ===== peer危险判断 =====

bool Abrv2Algorithm::IsPeerDangerous(int peerRep, int64_t segmentCounter,
                                     double peerTpMbps,
                                     int64_t peerBufferUs) const {
  double peerSens =
      CalcSensitivity(peerRep, segmentCounter, peerTpMbps, peerBufferUs);

  bool peerLowBuffer = peerBufferUs <= m_lowBufferThresholdUs;
  //   return peerLowBuffer || (peerSens > m_peerDangerTheta);
  return (peerSens > m_peerDangerTheta);
}

// 本方是否富裕
bool Abrv2Algorithm::IsSelfRich(int selfRep, int64_t segmentCounter,
                                double selfTpMbps, int64_t selfBufferUs) const {
  double selfSens =
      CalcSensitivity(selfRep, segmentCounter, selfTpMbps, selfBufferUs);

  bool selfLowBuffer = selfBufferUs <= m_lowBufferThresholdUs;

  return (selfSens < m_selfRichTheta);
}

// ===== 协同码率选择 =====

double Abrv2Algorithm::SelfSacrificeCost(int coopRep, int baseRep) const {
  double qLoss = std::max(0.0, QualityValue(baseRep) - QualityValue(coopRep));
  double sw = m_gammaSwitch *
              std::fabs(GetSelfSegmentSizeMbits(baseRep, m_selfsegmentCounter) -
                        GetSelfSegmentSizeMbits(coopRep, m_selfsegmentCounter));
  return m_lambdaSelfSacrifice * (qLoss + sw);
}

double Abrv2Algorithm::PeerBenefit(int coopRep, int baseRep, int peerRep,
                                   int64_t segmentCounter, double peerTpMbps,
                                   int64_t peerBufferUs) const {
  double eps = 0.01;

  // 这里不把“降码率”理解成直接让吞吐，而是理解成：
  // 降低本流未来资源需求后，更有利于后续停等。
  // 因此 peerBenefit 用“风险缓解潜力”近似。
  double selfBaseDemand =
      GetSelfSegmentSizeMbits(baseRep, segmentCounter) / (m_delta / 1e6);
  double selfCoopDemand =
      GetSelfSegmentSizeMbits(coopRep, segmentCounter) / (m_delta / 1e6);

  double releasedDemand = std::max(0.0, selfBaseDemand - selfCoopDemand);

  double peerSize = GetPeerSegmentSizeMbits(peerRep, m_peersegmentCounter);
  double beforeRisk = peerSize / (std::max(0.01, peerTpMbps) *
                                  std::max(eps, peerBufferUs / 1e6));

  // 用“需求下降 × 背景折扣”估计对后续停等创造的缓解潜力
  double afterTp = peerTpMbps + ComputeBackgroundDiscount() * releasedDemand;
  double afterRisk =
      peerSize / (std::max(0.01, afterTp) * std::max(eps, peerBufferUs / 1e6));

  return m_lambdaPeerBenefit * std::max(0.0, beforeRisk - afterRisk);
}

int Abrv2Algorithm::SelectCoopRep(int baseRep, int peerRep,
                                  int64_t segmentCounter, double peerTpMbps,
                                  int64_t peerBufferUs) const {
  int bestRep = baseRep;
  double bestGain = -1e18;

  for (int rep = 0; rep <= baseRep; ++rep) {
    double gain = PeerBenefit(rep, baseRep, peerRep, segmentCounter, peerTpMbps,
                              peerBufferUs) -
                  SelfSacrificeCost(rep, baseRep);

    if (gain > bestGain) {
      bestGain = gain;
      bestRep = rep;
    }
  }
  return bestRep;
}

// ===== 停等决策 =====

bool Abrv2Algorithm::HasPauseAbility(int coopRep, int64_t selfBufferUs,
                                     double selfTpMbps,
                                     int64_t segmentCounter) const {
  double nextDownloadTime = GetSelfSegmentSizeMbits(coopRep, segmentCounter) /
                            std::max(0.01, selfTpMbps);

  // 码率降低后，本流未来下载压力降低，因此允许的安全余量可以稍放松
  int64_t safeBufUs =
      std::max((int64_t)1000000,
               m_safetyMargin - (int64_t)(0.3 * nextDownloadTime * 1e6));

  int64_t dmaxSelf = std::max((int64_t)0, selfBufferUs - safeBufUs);
  return dmaxSelf > m_minPauseUs;
}

double Abrv2Algorithm::ComputeBackgroundDiscount() const {
  // 这里先简化成固定折扣
  // 背景流越多，这个值应越小
  return 0.7;
}

int64_t Abrv2Algorithm::ComputePauseDurationUs(
    int coopRep, int peerRep, int64_t segmentCounter, double selfTpMbps,
    double peerTpMbps, int64_t selfBufferUs, int64_t peerBufferUs) const {
  double eps = 0.01;

  // ===== 1. peer达到安全区所需吞吐 =====
  double peerSize = GetPeerSegmentSizeMbits(peerRep, m_peersegmentCounter);
  double Bp = peerBufferUs / 1e6;

  double Creq = peerSize / (m_peerDangerTheta * std::max(eps, Bp));

  // ===== 2. 停等后对方吞吐 =====
  // 不再加时间生效函数，默认背景流分走一部分后，其余都给peer
  double phi = ComputeBackgroundDiscount();
  double CpAfter = peerTpMbps + phi * selfTpMbps;

  if (CpAfter <= peerTpMbps) {
    return 0;
  }

  // ===== 3. 计算理论停等时长 =====
  //
  // 简化解释：
  // 一旦停等生效，peer 可获得 phi * selfTpMbps 的额外吞吐。
  // 因此需要的停等时长近似取为：
  // “peer 当前吞吐缺口 / 每秒可恢复吞吐” × 1秒
  //
  double throughputGap = std::max(0.0, Creq - peerTpMbps);
  if (throughputGap <= 0.0) {
    return 0;
  }

  double recoveredPerSecond = std::max(0.01, phi * selfTpMbps);
  double dStarSec = throughputGap / recoveredPerSecond;

  int64_t dStarUs = (int64_t)(dStarSec * 1e6);

  // ===== 4. self侧约束 =====
  double nextDownloadTime = GetSelfSegmentSizeMbits(coopRep, segmentCounter) /
                            std::max(0.01, selfTpMbps);
  int64_t safeBufUs =
      std::max((int64_t)1000000,
               m_safetyMargin - (int64_t)(0.3 * nextDownloadTime * 1e6));

  int64_t dmaxSelf = std::max((int64_t)0, selfBufferUs - safeBufUs);

  if (dmaxSelf <= m_minPauseUs) {
    return 0;
  }

  if (dStarUs < m_minPauseUs) {
    return 0;
  }

  return std::min(std::min(dStarUs, dmaxSelf), m_maxSystemPauseUs);
}

algorithmReply
Abrv2Algorithm::GetNextRepStandalone(const int64_t segmentCounter,
                                     int64_t clientId) {
  algorithmReply answer;
  int64_t timeNow = Simulator::Now().GetMicroSeconds();

  answer.decisionTime = timeNow;
  answer.nextDownloadDelay = 0;  // 默认不延迟，立即下载
  answer.delayDecisionCase = 0;

  /*
   * 第 0 个 segment：快速起播，最低码率
   */
  if (segmentCounter == 0) {
    answer.nextRepIndex = 0;
    answer.decisionCase = 0;
    return answer;
  }

  /*
   * ===== 1. 计算当前 buffer（秒）=====
   * 这一部分计算的不对需要修改
   *
   */
  // double bufferNow = (m_bufferData.bufferLevelNew.back() -
  //                     (timeNow - m_throughput.transmissionEnd.back())) /
  //                    1e6;
  int64_t bufferNow = ((double)m_bufferData.m_segmentsInBuffer *
                       m_bufferData.m_segmentDuration);
  NS_LOG_INFO("缓冲区水平:  " << bufferNow);
  /*
   * ===== 2. BBA 核心决策 =====
   */
  int nextRepIndex = 0;

  // 情况 A：buffer 太低，强制最低码率
  if (bufferNow <= m_reservoir) {
    nextRepIndex = 0;
    answer.decisionCase = 1;  // low-buffer protection
  }
  // 情况 B：buffer 很高，直接最高码率
  else if (bufferNow >= m_targetBuf - 2 * m_delta) {
    nextRepIndex = m_highestRepIndex;
    answer.decisionCase = 2;  // high-buffer
    // 如果当前的缓冲区水平已经达到最大的缓冲区，则暂停下载一段时间
    int64_t lowerBound = (m_targetBuf - m_delta);
    int64_t upperBound = (m_targetBuf + m_delta);
    // 随机生成一个 buffer 目标值
    int64_t randBuf =
        (int64_t)lowerBound + (std::rand() % (upperBound - (lowerBound) + 1));
    NS_LOG_INFO("lowerBound:  " << lowerBound << "  upperBound:  " << upperBound
                                << "  m_delta:  " << m_delta);
    NS_LOG_INFO("缓冲区水平:  "
                << bufferNow << "  与随机的休息缓冲区:  " << randBuf
                << "  固定休息缓冲区:  " << m_targetBuf - 3 * m_delta);
    // [延迟下载决策] 如果当前缓冲 > 随机目标，说明缓冲太足了，休息一会
    // 休息时间 = 多出来的这部分时间
    if (bufferNow >= m_targetBuf - 2 * m_delta) {
      answer.nextDownloadDelay = bufferNow - m_targetBuf + 3 * m_delta;
      // answer.nextDownloadDelay = 0;
      answer.delayDecisionCase = 1;
    }
  }
  // 情况 C：buffer 位于中间，线性映射
  else {
    double fraction = (bufferNow - m_reservoir) / m_cushion;

    // 映射到码率索引 向下取整
    nextRepIndex = (int)std::floor(fraction * m_highestRepIndex);

    // 防止越界
    nextRepIndex = std::max(0, std::min(nextRepIndex, m_highestRepIndex));

    answer.decisionCase = 4;  // linear mapping
  }

  answer.nextRepIndex = nextRepIndex;
  return answer;
}

// ===== 主决策 =====

algorithmReply
Abrv2Algorithm::GetNextRep(const int64_t segmentCounter, int64_t clientId) {
  // 1. 基础码率选择
  // 非共享瓶颈：直接返回基础码率
  algorithmReply answer = GetNextRepStandalone(segmentCounter, clientId);
  if (!m_bufferData.isInSB) {
    return answer;
  }
  int64_t baseRep = answer.nextRepIndex;

  m_selfsegmentCounter = segmentCounter;
  m_peersegmentCounter = m_peerbufferData.m_segmentCounter;

  double selfTpMbps = ComputeWeightedThroughputMbps(m_throughput, 10, 0.7);
  double peerTpMbps = ComputeWeightedThroughputMbps(m_peerthroughput, 10, 0.7);

  int64_t selfBufferUs = GetSelfBufferUs();
  int64_t peerBufferUs = GetPeerBufferUs();

  int peerRep = m_peerplaybackData.playbackIndex.back();

  // 2.0 判断本方是否富裕
  bool selfRich = IsSelfRich(baseRep, segmentCounter, selfTpMbps, selfBufferUs);
  // 2.1 判断对方是否危险
  bool peerDanger =
      IsPeerDangerous(peerRep, segmentCounter, peerTpMbps, peerBufferUs);
  // 如果不危险，直接还是返回BBA的决策
  if (!peerDanger) {
    return answer;
  }

  // 危险了，需要协同决策了
  // 3. 协同码率选择
  int coopRep =
      SelectCoopRep(baseRep, peerRep, segmentCounter, peerTpMbps, peerBufferUs);

  answer.nextRepIndex = coopRep;
  answer.decisionCase = 13;

  // 4. 停等决策
  if (HasPauseAbility(coopRep, selfBufferUs, selfTpMbps, segmentCounter)) {
    int64_t delayUs =
        ComputePauseDurationUs(coopRep, peerRep, segmentCounter, selfTpMbps,
                               peerTpMbps, selfBufferUs, peerBufferUs);

    if (delayUs > 0) {
      answer.nextDownloadDelay = delayUs;
      answer.delayDecisionCase = 1;
      answer.decisionCase = 14;
    }
  }

  //   NS_LOG_INFO("Client " << clientId << " Segment " << segmentCounter
  //                         << " BaseRep: " << baseRep << " PeerRep: " <<
  //                         peerRep
  //                         << " CoopRep: " << coopRep << " SelfTpMbps: "
  //                         << selfTpMbps << " PeerTpMbps: " << peerTpMbps
  //                         << " SelfBufferUs: " << selfBufferUs
  //                         << " PeerBufferUs: " << peerBufferUs
  //                         << " NextDownloadDelay: " <<
  //                         answer.nextDownloadDelay);
  return answer;
}

}  // namespace ns3
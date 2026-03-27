#include "abr.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("AbrAlgorithm");
NS_OBJECT_ENSURE_REGISTERED(AbrAlgorithm);

AbrAlgorithm::AbrAlgorithm(
    const videoData &selfvideoData, const videoData &peervideoData,
    const playbackData &selfplaybackData, const playbackData &peerplaybackData,
    const bufferData &selfbufferData, const bufferData &peerbufferData,
    const throughputData &selfthroughput, throughputData &peerthroughput)
    : AdaptationAlgorithm(selfvideoData, selfplaybackData, selfbufferData,
                          selfthroughput),
      m_delta(selfbufferData.m_segmentDuration),
      m_peervideoData(peervideoData),
      m_peerplaybackData(peerplaybackData),
      m_peerbufferData(peerbufferData),
      m_peerthroughput(peerthroughput),
      m_highestRepIndex(selfvideoData.averageBitrate.size() - 1),
      m_lowBufferThresholdUs(6000000),
      m_highBufferThresholdUs(9000000),
      m_veryHighBufferThresholdUs(12000000),
      m_safetyMargin(3000000),
      m_minPauseUs(selfbufferData.m_segmentDuration / 4),
      m_maxSystemPauseUs(2 * selfbufferData.m_segmentDuration),
      m_maxBufferUs(24000000)
// m_maxBufferUs(30000000)
{
  NS_ASSERT_MSG(m_highestRepIndex >= 0,
                "The highest quality representation index should be >= 0");
}

int64_t AbrAlgorithm::GetSelfBufferUs() const {
  return m_bufferData.m_segmentsInBuffer * m_bufferData.m_segmentDuration;
}

int64_t AbrAlgorithm::GetPeerBufferUs() const {
  return m_peerbufferData.m_segmentsInBuffer *
         m_peerbufferData.m_segmentDuration;
}

int64_t AbrAlgorithm::GetSelfBufferedData() const {
  return std::accumulate(m_bufferData.segmentSizes.begin(),
                         m_bufferData.segmentSizes.end(), (int64_t)0);
}

int64_t AbrAlgorithm::GetPeerBufferedData() const {
  return std::accumulate(m_peerbufferData.segmentSizes.begin(),
                         m_peerbufferData.segmentSizes.end(), (int64_t)0);
}

int AbrAlgorithm::StepDownRep(int rep, int steps) const {
  return std::max(0, rep - steps);
}

double AbrAlgorithm::ComputeWeightedThroughputMbps(const throughputData &data,
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
  double throughputMbps = weightedBits / weightedUs;

  // 如果对方流处于停等状态，那么我即将获得的吞吐是让给对方流的，不能乐观估计吞吐，应该打个折扣
  if (data.peerIsStalling) {
    NS_LOG_INFO("Peer is stalling, applying safety margin to throughput"
                << throughputMbps << " Mbps" << " to " << throughputMbps / 2
                << " Mbps");

    throughputMbps = throughputMbps * 0.5;
  }
  NS_LOG_INFO("Computed weighted throughput: " << throughputMbps << " Mbps");
  return throughputMbps;
}

// ===== representation 对应参数 =====

double
AbrAlgorithm::GetSelfSegmentSizeMbits(int rep, int64_t segmentCounter) const {
  rep = std::max(0, std::min(rep, m_highestRepIndex));
  return m_videoData.segmentSize[rep][segmentCounter] * 8.0 / 1000000.0;
}

double
AbrAlgorithm::GetPeerSegmentSizeMbits(int rep, int64_t segmentCounter) const {
  rep = std::max(0, std::min(rep, m_highestRepIndex));
  return m_peervideoData.segmentSize[rep][segmentCounter] * 8.0 / 1000000.0;
}

// ===== 风险度量化 =====

double AbrAlgorithm::BufferDangerPressure(int64_t bufferUs) const {
  double Bref = (double)m_lowBufferThresholdUs;
  double B = (double)bufferUs;
  return std::max(0.0, (Bref - B) / std::max(1.0, Bref));
}

double AbrAlgorithm::CalcSensitivity(int rep, int64_t segmentCounter,
                                     double throughputMbps, int64_t bufferUs,
                                     int64_t bufferedDataBits) const {
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

// ===== 基础码率选择各分量 =====

double
AbrAlgorithm::RebufferPenalty(int rep, int64_t segmentCounter,
                              double throughputMbps, int64_t bufferUs) const {
  double downloadTime = GetSelfSegmentSizeMbits(rep, m_selfsegmentCounter) /
                        std::max(0.01, throughputMbps);
  double bufferSec = bufferUs / 1e6;
  return m_alphaRebuffer * std::max(0.0, downloadTime - bufferSec);
}

double AbrAlgorithm::ImbalancePenalty(int rep) const {
  double selfVal =
      GetSelfSegmentSizeMbits(rep, m_selfsegmentCounter) /
      GetSelfSegmentSizeMbits(m_highestRepIndex, m_selfsegmentCounter);
  double peerVal =
      GetPeerSegmentSizeMbits(m_peerplaybackData.playbackIndex.back(),
                              m_peersegmentCounter) /
      GetPeerSegmentSizeMbits(m_highestRepIndex, m_peersegmentCounter);

  NS_LOG_INFO("ImbalancePenalty: selfVal=" << selfVal
                                           << " peerVal=" << peerVal);

  return m_betaImbalance * std::fabs(selfVal - peerVal);
}

double AbrAlgorithm::SwitchPenalty(int rep) const {
  double newVal = GetSelfSegmentSizeMbits(rep, m_selfsegmentCounter);
  double curVal = m_bufferData.segmentSizes.back() * 8.0 / 1000000.0;
  double res = 0;
  if (m_bufferData.isAudio)
    res = m_gammaAudioSwitch * std::fabs(newVal - curVal);
  else
    res = m_gammaVideoSwitch * std::fabs(newVal - curVal);
  return res;
}

double AbrAlgorithm::SensitivityPenalty(int rep, int64_t segmentCounter,
                                        double throughputMbps, int64_t bufferUs,
                                        int64_t bufferedDataBits) const {
  return m_etaSensitivity * CalcSensitivity(rep, segmentCounter, throughputMbps,
                                            bufferUs, bufferedDataBits);
}

double AbrAlgorithm::BaseRepScore(int rep, int64_t segmentCounter,
                                  double selfTpMbps, int64_t selfBufferUs,
                                  int64_t selfBufferedBits) const {
  double res1 = GetSelfSegmentSizeMbits(rep, segmentCounter);
  double res2 = RebufferPenalty(rep, segmentCounter, selfTpMbps, selfBufferUs);
  double res3 = ImbalancePenalty(rep);
  double res4 = SwitchPenalty(rep);
  //   double res5 = SensitivityPenalty(rep, segmentCounter, selfTpMbps,
  //                                    selfBufferUs, selfBufferedBits);

  NS_LOG_INFO("QualityValue=" << res1 << ", RebufferPenalty=" << res2
                              << ", ImbalancePenalty=" << res3
                              << ", SwitchPenalty=" << res4);
  double res = res1 - res2 - res3 - res4;
  return res;
}

int AbrAlgorithm::SelectBaseRep(int64_t segmentCounter, double selfTpMbps,
                                int64_t selfBufferUs,
                                int64_t selfBufferedBits) const {
  int bestRep = 0;
  double bestScore = -1e18;

  for (int rep = 0; rep <= m_highestRepIndex; ++rep) {
    double score = BaseRepScore(rep, segmentCounter, selfTpMbps, selfBufferUs,
                                selfBufferedBits);
    NS_LOG_INFO(" Segment " << segmentCounter << " Rep " << rep
                            << " Score: " << score);
    if (score > bestScore) {
      bestScore = score;
      bestRep = rep;
    }
  }
  NS_LOG_INFO(" Segment " << segmentCounter
                          << "Selected base rep: " << bestRep);
  return bestRep;
}

// ===== peer危险判断 =====

bool AbrAlgorithm::IsSharedBottleneck() const {
  return m_bufferData.isInSB && m_peerbufferData.isInSB;
}

bool AbrAlgorithm::IsPeerDangerous(int peerRep, int64_t segmentCounter,
                                   double peerTpMbps, int64_t peerBufferUs,
                                   int64_t peerBufferedBits) const {
  double peerSens = CalcSensitivity(peerRep, segmentCounter, peerTpMbps,
                                    peerBufferUs, peerBufferedBits);

  bool peerLowBuffer = peerBufferUs <= m_lowBufferThresholdUs;
  return peerLowBuffer && (peerSens > m_peerDangerTheta);
}

// ===== 协同码率选择 =====

double AbrAlgorithm::SelfSacrificeCost(int coopRep, int baseRep) const {
  double qLoss =
      std::max(0.0, GetSelfSegmentSizeMbits(baseRep, m_selfsegmentCounter) -
                        GetSelfSegmentSizeMbits(coopRep, m_selfsegmentCounter));
  double sw = 0;
  if (m_bufferData.isAudio)
    sw = m_gammaAudioSwitch *
         std::fabs(GetSelfSegmentSizeMbits(baseRep, m_selfsegmentCounter) -
                   GetSelfSegmentSizeMbits(coopRep, m_selfsegmentCounter));
  else
    sw = m_gammaVideoSwitch *
         std::fabs(GetSelfSegmentSizeMbits(baseRep, m_selfsegmentCounter) -
                   GetSelfSegmentSizeMbits(coopRep, m_selfsegmentCounter));

  return m_lambdaSelfSacrifice * (qLoss + sw);
}

double AbrAlgorithm::PeerBenefit(int coopRep, int baseRep, int peerRep,
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

int AbrAlgorithm::SelectCoopRep(int baseRep, int peerRep,
                                int64_t segmentCounter, double peerTpMbps,
                                int64_t peerBufferUs) const {
  int bestRep = baseRep;
  double bestGain = -1e18;

  for (int rep = 0; rep <= baseRep; ++rep) {
    double benefit = PeerBenefit(rep, baseRep, peerRep, segmentCounter,
                                 peerTpMbps, peerBufferUs);
    double sacrifice = SelfSacrificeCost(rep, baseRep);
    double gain = benefit - sacrifice;

    NS_LOG_INFO(" Coop Rep " << rep << " Benefit: " << benefit << " Sacrifice: "
                             << sacrifice << " Gain: " << gain);

    if (gain > bestGain) {
      bestGain = gain;
      bestRep = rep;
    }
  }
  NS_LOG_INFO("segmentCounter" << segmentCounter
                               << "Selected coop rep: " << bestRep);
  return bestRep;
}

// ===== 停等决策 =====

bool AbrAlgorithm::HasPauseAbility(int coopRep, int64_t selfBufferUs,
                                   double selfTpMbps,
                                   int64_t segmentCounter) const {
  double nextDownloadTime = GetSelfSegmentSizeMbits(coopRep, segmentCounter) /
                            std::max(0.01, selfTpMbps);

  // 码率降低后，本流未来下载压力降低，因此允许的安全余量可以稍放松
  int64_t safeBufUs = std::max(
      (int64_t)1000000, m_safetyMargin - (int64_t)(nextDownloadTime * 1e6));
  NS_LOG_INFO("HasPauseAbility: nextDownloadTime="
              << nextDownloadTime << " safeBuf=" << safeBufUs / 1000000.0);

  int64_t dmaxSelf = std::max((int64_t)0, selfBufferUs - safeBufUs);
  return dmaxSelf > m_minPauseUs;
}

double AbrAlgorithm::ComputeBackgroundDiscount() const {
  // 这里先简化成固定折扣
  // 背景流越多，这个值应越小
  return 0.7;
}

int64_t
AbrAlgorithm::ComputePauseDurationUs(int coopRep, int peerRep,
                                     int64_t segmentCounter, double selfTpMbps,
                                     double peerTpMbps, int64_t selfBufferUs,
                                     int64_t peerBufferUs) const {
  double eps = 1e-6;

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

  double recoveredPerSecond = std::max(eps, phi * selfTpMbps);
  double dStarSec = throughputGap / recoveredPerSecond;

  int64_t dStarUs = (int64_t)(dStarSec * 1e6);

  // ===== 4. self侧约束 =====
  double nextDownloadTime = GetSelfSegmentSizeMbits(coopRep, segmentCounter) /
                            std::max(eps, selfTpMbps);
  int64_t safeBufUs =
      std::max((int64_t)1000000,
               m_safetyMargin - (int64_t)(0.6 * nextDownloadTime * 1e6));

  int64_t dmaxSelf = std::max((int64_t)0, selfBufferUs - safeBufUs);

  if (dmaxSelf <= m_minPauseUs) {
    return 0;
  }

  if (dStarUs < m_minPauseUs) {
    return 0;
  }

  return std::min(std::min(dStarUs, dmaxSelf), m_maxSystemPauseUs);
}

// ===== 主决策 =====

algorithmReply
AbrAlgorithm::GetNextRep(const int64_t segmentCounter, int64_t clientId) {
  algorithmReply answer;
  int64_t timeNow = Simulator::Now().GetMicroSeconds();
  m_selfsegmentCounter = segmentCounter;
  m_peersegmentCounter = m_peerbufferData.m_segmentCounter;

  answer.decisionTime = timeNow;
  answer.nextDownloadDelay = 0;
  answer.delayDecisionCase = 0;
  answer.nextRepIndex = 0;
  answer.decisionCase = 0;

  if (segmentCounter == 0) {
    answer.nextRepIndex = 0;
    answer.decisionCase = 0;
    return answer;
  }

  NS_LOG_INFO("selfTpMbps");
  double selfTpMbps = ComputeWeightedThroughputMbps(m_throughput, 10, 0.7);

  NS_LOG_INFO("peerTpMbps");
  double peerTpMbps = ComputeWeightedThroughputMbps(m_peerthroughput, 10, 0.7);

  int64_t selfBufferUs = GetSelfBufferUs();
  int64_t peerBufferUs = GetPeerBufferUs();

  int64_t selfBufferedBits = GetSelfBufferedData() * 8;
  int64_t peerBufferedBits = GetPeerBufferedData() * 8;

  // 1. 基础码率选择
  int baseRep =
      SelectBaseRep(segmentCounter, selfTpMbps, selfBufferUs, selfBufferedBits);
  // 0. 满缓冲控制：如果本流缓冲区已经达到上限，则主动暂停请求
  if (selfBufferUs >= m_maxBufferUs) {
    int64_t minPauseUs = 1 * m_delta;
    int64_t maxPauseUs = 3 * m_delta;

    int64_t randomPauseUs =
        minPauseUs + (std::rand() % (maxPauseUs - minPauseUs + 1));

    // 当前不需要特别改码率，保持当前基础码率或最低码率都可以
    // 这里建议保持当前播放码率对应的 index
    answer.nextRepIndex = m_playbackData.playbackIndex.back();
    // answer.nextRepIndex = baseRep;
    answer.nextDownloadDelay = randomPauseUs;
    answer.delayDecisionCase = 99;  // 自定义：满缓冲暂停
    answer.decisionCase = 99;
    // m_peerthroughput.peerIsStalling = true;  // 告诉对方流我即将停等

    NS_LOG_INFO("Buffer reaches max 30s, pause request for "
                << answer.nextDownloadDelay / 1000000 << " s");
    return answer;
  } else {
    // m_peerthroughput.peerIsStalling = false;  // 告诉对方流我不处于停等状态
  }

  // m_peerthroughput.peerIsStalling = false;

  // 非共享瓶颈：直接返回基础码率
  if (!IsSharedBottleneck()) {
    answer.nextRepIndex = baseRep;
    answer.decisionCase = 1;
    return answer;
  }

  // 获取对方最新设置的码率
  int peerRep = m_peerplaybackData.playbackIndex.back();

  // 2. 判断对方是否危险
  bool peerDanger = IsPeerDangerous(peerRep, segmentCounter, peerTpMbps,
                                    peerBufferUs, peerBufferedBits);

  if (!peerDanger) {
    answer.nextRepIndex = baseRep;
    answer.decisionCase = 2;
    return answer;
  }

  // 本流足够富裕
  // if (IsSelfRich(baseRep, segmentCounter, selfTpMbps, selfBufferUs,
  //                selfBufferedBits)) {
  //   answer.nextRepIndex = baseRep;
  //   answer.decisionCase = 21;  // 自定义：对方危险但自己富裕，不牺牲
  //   return answer;
  // }
  // 3. 协同码率选择
  int coopRep =
      SelectCoopRep(baseRep, peerRep, segmentCounter, peerTpMbps, peerBufferUs);

  answer.nextRepIndex = coopRep;
  answer.decisionCase = 3;

  // 4. 停等决策
  if (HasPauseAbility(coopRep, selfBufferUs, selfTpMbps, segmentCounter)) {
    int64_t delayUs =
        ComputePauseDurationUs(coopRep, peerRep, segmentCounter, selfTpMbps,
                               peerTpMbps, selfBufferUs, peerBufferUs);

    if (delayUs > 0) {
      /*
      做出来停等决策，那么需要让对方流知道
      这个吞吐是让给它的，它需要加以节制的使用这个吞吐
      不能乐观的估计吞吐，导致决策出不利的rep，反而伤害自己和对方的体验
      */
      answer.nextDownloadDelay = delayUs;
      answer.delayDecisionCase = 1;
      answer.decisionCase = 4;

      m_peerthroughput.peerIsStalling = true;  // 告诉对方流我即将停等
      NS_LOG_INFO("Decide to pause for "
                  << delayUs / 1000000.0
                  << " s to help peer, applying safety margin to throughput");
    } else {
      m_peerthroughput.peerIsStalling = false;
    }
  }

  NS_LOG_INFO(" Segment " << segmentCounter << " BaseRep: " << baseRep
                          << " CoopRep: " << coopRep << " PeerRep: " << peerRep
                          << " SelfTpMbps: " << selfTpMbps << " PeerTpMbps: "
                          << peerTpMbps << " SelfBufferUs: " << selfBufferUs
                          << " PeerBufferUs: " << peerBufferUs
                          << " NextDownloadDelay: "
                          << answer.nextDownloadDelay);
  return answer;
}

}  // namespace ns3
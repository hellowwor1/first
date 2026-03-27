#include "sabba.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SabbaAlgorithm");
NS_OBJECT_ENSURE_REGISTERED(SabbaAlgorithm);

SabbaAlgorithm::SabbaAlgorithm(const videoData &videoData,
                               const playbackData &playbackData,
                               const bufferData &selfbufferData,
                               const bufferData &peerbufferData,
                               const throughputData &selfthroughput,
                               const throughputData &peerthroughput)
    : AdaptationAlgorithm(videoData, playbackData, selfbufferData,
                          selfthroughput),
      // m_reservoir(8000000.0),  // 10 秒安全缓冲
      // m_cushion(24000000.0),   // 40 秒线性映射区间
      // m_targetBuf(40000000),   // 60s的缓冲区水平
      m_reservoir(6000000.0),  // 6 秒安全缓冲
      m_cushion(20000000.0),   // 20 秒线性映射区间
      m_targetBuf(30000000),   // 30s的缓冲区水平
      m_delta(selfbufferData.m_segmentDuration),
      m_peerbufferData(peerbufferData),
      m_peerthroughput(peerthroughput),
      m_highestRepIndex(videoData.averageBitrate.size() - 1),
      // m_lowBufferThresholdUs(4000000),       // 4s
      m_lowBufferThresholdUs(6000000),        // 2s
      m_highBufferThresholdUs(9000000),       // 8s
      m_veryHighBufferThresholdUs(10000000),  // 10s
      m_safetyMargin(3000000)                 // 3s
{
  NS_LOG_INFO(this);
  NS_ASSERT_MSG(m_highestRepIndex >= 0,
                "The highest quality representation index should be >= 0");
}
int64_t SabbaAlgorithm::GetSelfBufferUs() const {
  return m_bufferData.m_segmentsInBuffer * m_bufferData.m_segmentDuration;
}

int64_t SabbaAlgorithm::GetPeerBufferUs() const {
  return m_peerbufferData.m_segmentsInBuffer *
         m_peerbufferData.m_segmentDuration;
}

int SabbaAlgorithm::StepDownRep(int rep, int steps) const {
  return std::max(0, rep - steps);
}

int64_t SabbaAlgorithm::GetSelfBufferedData() const {
  return std::accumulate(m_bufferData.segmentSizes.begin(),
                         m_bufferData.segmentSizes.end(), (int64_t)0);
}

int64_t SabbaAlgorithm::GetPeerBufferedData() const {
  return std::accumulate(m_peerbufferData.segmentSizes.begin(),
                         m_peerbufferData.segmentSizes.end(), (int64_t)0);
}

double SabbaAlgorithm::ComputeWeightedThroughputMbps(const throughputData &data,
                                                     size_t recentCount = 10,
                                                     double alpha = 0.7) {
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

// 当发生共享瓶颈时，而且对方缓冲区的水平比较糟糕，本方缓冲区情况较好时，采用耦合的ABR决策函数
/*
 * 流独自的  ABR 决策函数
 */
algorithmReply
SabbaAlgorithm::GetNextRep(const int64_t segmentCounter, int64_t clientId) {
  // 未进入共享瓶颈状态，正常设置码率
  if (!m_bufferData.isInSB) {
    inSBstate = 0;
    return GetNextRepStandalone(segmentCounter, clientId);
  }
  NS_LOG_INFO(
      "进入共享瓶颈状态，依据双方缓冲区的状况判断是否要降低码率、停止请求");
  NS_LOG_INFO("QoE in ABR" << *m_bufferData.qoe);
  double selfthroughput = ComputeWeightedThroughputMbps(m_throughput, 10, 0.7);
  double peerthroughput =
      ComputeWeightedThroughputMbps(m_peerthroughput, 10, 0.7);
  NS_LOG_INFO("selfthroughput=" << selfthroughput << " Mbps, peerthroughput="
                                << peerthroughput << " Mbps");
  // 进入共享瓶颈状态，依据双方缓冲区的状况判断是否要降低码率、停止请求
  // 先基于单流 BBA 得到基础决策
  algorithmReply answer = GetNextRepStandalone(segmentCounter, clientId);
  int64_t timeNow = Simulator::Now().GetMicroSeconds();
  answer.decisionTime = timeNow;
  // answer.nextDownloadDelay = 0;
  // answer.delayDecisionCase = 0;

  int64_t selfBufferUs = GetSelfBufferUs();
  int64_t peerBufferUs = GetPeerBufferUs();

  int64_t selfBufferedData = GetSelfBufferedData();
  int64_t peerBufferedData = GetPeerBufferedData();

  bool selfLowTime = (selfBufferUs <= m_lowBufferThresholdUs);
  bool peerLowTime = (peerBufferUs <= m_lowBufferThresholdUs);

  double dataRatio =
      (double)selfBufferedData / std::max((int64_t)1, peerBufferedData);

  bool selfHighData = (dataRatio >= 1.5);
  bool selfVeryHighData = (dataRatio >= 3.0);

  // 1. 如果我是弱势方，不应该主动让步
  if (selfLowTime) {
    inSBstate = 1;
    answer.decisionCase = 10;
    m_consecutiveYieldCount = 0;
    NS_LOG_INFO("Shared bottleneck: 本流是弱势方，不应该主动让步");
    return answer;
  }
  // 2. 如果对方不低缓冲，就不需要让我来让步
  if (!peerLowTime) {
    inSBstate = 0;
    answer.decisionCase = 11;
    m_consecutiveYieldCount = 0;
    NS_LOG_INFO("Shared bottleneck: 对方流不低缓冲，就不需要让步");
    return answer;
  }
  // 3. 到这里说明：共享瓶颈 + 对方低缓冲 + 我不低缓冲
  //    我作为更富裕的一方，主动让步
  if (selfHighData) {
    int stepDown = 1;
    m_consecutiveYieldCount++;
    // 如果已经连续让步很多次，但对方仍然低缓冲，直接休眠让路
    if (m_consecutiveYieldCount >= m_yieldEscalationThreshold) {
      inSBstate = 3;
      answer.nextRepIndex = 0;  // 可选：休眠前顺便降到最低
      answer.decisionCase = 15;

      // 强制让路休眠时间：建议 1~4 个 segment duration
      int64_t minPause = 2 * m_delta;
      int64_t maxPause = 4 * m_delta;
      int64_t forcedPause =
          minPause + (std::rand() % (maxPause - minPause + 1));

      // ===== 新增：buffer约束 =====
      // 保证不会把buffer耗尽（保留安全buffer）
      // 最大允许暂停时间
      int64_t maxAllowedPause =
          std::max((int64_t)0, selfBufferUs - m_safetyMargin);
      NS_LOG_INFO("selfBufferUs=" << selfBufferUs
                                  << "  m_safetyMargin=" << m_safetyMargin
                                  << "  maxAllowedPause=" << maxAllowedPause
                                  << "  forcedPause=" << forcedPause);
      // clamp
      forcedPause = std::min(forcedPause, maxAllowedPause);
      answer.delayDecisionCase = 15;
      // ===== 合并delay =====
      answer.nextDownloadDelay =
          std::max(answer.nextDownloadDelay, forcedPause);
      NS_LOG_INFO("Shared bottleneck: 连续让步无效，升级为强制让路休眠 "
                  << answer.nextDownloadDelay / 1e6 << "s"
                  << ", yieldCount=" << m_consecutiveYieldCount);
      return answer;
    }

    // 自己 buffer 非常高时，退让更积极一点
    // buffer 过高 已经达到满缓冲
    if (selfVeryHighData) {
      stepDown = 3;
    }

    answer.nextRepIndex = StepDownRep(answer.nextRepIndex, stepDown);
    answer.decisionCase = 12;
    inSBstate = 2;

    NS_LOG_INFO("Shared bottleneck: 主动让步, 让步="
                << stepDown << ", 新码率为=" << answer.nextRepIndex);

    // 4. 如果我非常富裕而对方又非常危险，则再额外暂停一点时间
    if (selfVeryHighData && peerBufferUs <= (m_lowBufferThresholdUs / 2)) {
      int64_t excessUs = selfBufferUs - m_highBufferThresholdUs;
      int64_t extraDelay = std::max((int64_t)0, excessUs / 2);
      // 只用一部分富余缓冲来换 delay，避免暂停过长
      answer.nextDownloadDelay += extraDelay;
      answer.delayDecisionCase = 13;

      inSBstate = 3;

      NS_LOG_INFO("Shared bottleneck: 额外暂停, 暂停="
                  << answer.nextDownloadDelay / 1e6 << "s");
    }

    return answer;
  }
  // 5. 其他情况：虽然对方 buffer 低，但我也没有高到值得让步
  answer.decisionCase = 14;
  inSBstate = 1;
  NS_LOG_INFO(
      "Shared bottleneck: 虽然对方 buffer 低，但本流也没有高到值得让步");
  return answer;
}

algorithmReply
SabbaAlgorithm::GetNextRepStandalone(const int64_t segmentCounter,
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

}  // namespace ns3

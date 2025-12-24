#include "bba.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("BbaAlgorithm");
NS_OBJECT_ENSURE_REGISTERED(BbaAlgorithm);

BbaAlgorithm::BbaAlgorithm(const videoData &videoData,
                           const playbackData &playbackData,
                           const bufferData &bufferData,
                           const throughputData &throughput)
    : AdaptationAlgorithm(videoData, playbackData, bufferData, throughput),
      m_reservoir(20.0),  // 20 秒安全缓冲
      m_cushion(30.0),    // 30 秒线性映射区间
      m_highestRepIndex(videoData.averageBitrate.size() - 1) {
  NS_LOG_INFO(this);
  NS_ASSERT_MSG(m_highestRepIndex >= 0,
                "The highest quality representation index should be >= 0");
}

/*
 * 核心 ABR 决策函数
 */
algorithmReply
BbaAlgorithm::GetNextRep(const int64_t segmentCounter, int64_t clientId) {
  algorithmReply answer;
  int64_t timeNow = Simulator::Now().GetMicroSeconds();

  answer.decisionTime = timeNow;
  answer.nextDownloadDelay = 0;  // BBA 不主动延迟请求
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
  double bufferNow = (m_bufferData.bufferLevelNew.back() -
                      (timeNow - m_throughput.transmissionEnd.back())) /
                     1e6;

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
  else if (bufferNow >= (m_reservoir + m_cushion)) {
    nextRepIndex = m_highestRepIndex;
    answer.decisionCase = 2;  // high-buffer
  }
  // 情况 C：buffer 位于中间，线性映射
  else {
    double fraction = (bufferNow - m_reservoir) / m_cushion;

    // 映射到码率索引 向下取整
    nextRepIndex = (int)std::floor(fraction * m_highestRepIndex);

    // 防止越界
    nextRepIndex = std::max(0, std::min(nextRepIndex, m_highestRepIndex));

    answer.decisionCase = 3;  // linear mapping
  }

  answer.nextRepIndex = nextRepIndex;
  return answer;
}

}  // namespace ns3

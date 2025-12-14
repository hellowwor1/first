/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "audio-simple-algorithm.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("AudioSimpleAlgorithm");

NS_OBJECT_ENSURE_REGISTERED(AudioSimpleAlgorithm);

// --- 构造函数 ---
AudioSimpleAlgorithm::AudioSimpleAlgorithm(const videoData& videoData,
                                           const playbackData& playbackData,
                                           const bufferData& bufferData,
                                           const throughputData& throughput)
    : AdaptationAlgorithm(videoData, playbackData, bufferData, throughput),
      m_targetBuf(60000000),  // [修改点1] 目标缓冲设为 60秒
                              // (视频通常30秒)。音频小，多存点。
      m_thrptThrsh(0.95),  // [修改点2] 阈值设为 0.95。音频流稳定，可以利用 95%
                           // 的估算带宽。
      m_highestRepIndex(videoData.averageBitrate.size() - 1),
      m_stableCounter(0) {
  NS_LOG_INFO(this);
}

algorithmReply AudioSimpleAlgorithm::GetNextRep(const int64_t segmentCounter,
                                                int64_t clientId) {
  int64_t timeNow = Simulator::Now().GetMicroSeconds();
  algorithmReply answer;
  answer.decisionTime = timeNow;
  answer.nextDownloadDelay = 0;
  answer.delayDecisionCase = 0;

  // 1. 起播阶段：直接选最低音质，确保秒开
  if (segmentCounter == 0) {
    answer.nextRepIndex = 0;
    answer.decisionCase = 0;
    return answer;
  }

  // 计算当前缓冲水位
  int64_t bufferNow = m_bufferData.bufferLevelNew.back() -
                      (timeNow - m_throughput.transmissionEnd.back());

  // [修改点3] 简单的休眠逻辑
  // 如果缓冲超过 60秒，就暂停下载，直到缓冲降到 60秒以下。
  // 不需要Festive那种复杂的随机化，音频简单直接最好。
  if (bufferNow > m_targetBuf) {
    answer.nextDownloadDelay = bufferNow - m_targetBuf;
    // 保持当前码率，但延迟请求
    answer.nextRepIndex = m_playbackData.playbackIndex.back();
    answer.decisionCase = 1;
    answer.nextDownloadDelay = 1;
    return answer;
  }

  // 2. 带宽估算 (保留 Festive 的调和平均数逻辑，因为它抗干扰能力强)
  // 如果样本太少，保持最低音质
  if (m_throughput.transmissionEnd.size() <
      5)  // 音频切片小，5个样本大概就能看了
  {
    answer.nextRepIndex = 0;
    answer.decisionCase = 2;
    return answer;
  }

  std::vector<double> thrptEstimationTmp;
  for (unsigned sd = m_playbackData.playbackIndex.size(); sd-- > 0;) {
    if (m_throughput.bytesReceived.at(sd) == 0) continue;
    // 计算吞吐量 (bps)
    thrptEstimationTmp.push_back(
        (8.0 * m_throughput.bytesReceived.at(sd)) /
        ((double)((m_throughput.transmissionEnd.at(sd) -
                   m_throughput.transmissionRequested.at(sd)) /
                  1000000.0)));
    if (thrptEstimationTmp.size() == 10) break;  // 音频取最近10个样本即可
  }

  double harmonicMeanDenominator = 0;
  for (const auto& val : thrptEstimationTmp) {
    harmonicMeanDenominator += 1.0 / val;
  }
  double thrptEstimation = thrptEstimationTmp.size() / harmonicMeanDenominator;

  // 3. 码率选择逻辑 (简化版)
  int64_t currentRepIndex = m_playbackData.playbackIndex.back();
  int64_t nextRepIndex = currentRepIndex;

  // [紧急降级]：缓冲太低 (<10秒) 或者 带宽严重不足
  // 如果带宽连当前码率的 95% 都达不到，必须立刻降级，保流畅。
  if (m_videoData.averageBitrate.at(currentRepIndex) >
      thrptEstimation * m_thrptThrsh) {
    if (currentRepIndex > 0) {
      nextRepIndex = currentRepIndex - 1;
      m_stableCounter = 0;  // 重置稳定计数器
    }
  }
  // [保守升级]
  // 只有当：
  // 1. 还没到最高音质
  // 2. 下一档音质的码率 < 估算带宽 (安全的)
  // 3. 已经在当前音质稳定了 3 个切片 (防止来回跳变)
  else if (currentRepIndex < m_highestRepIndex) {
    if ((double)m_videoData.averageBitrate.at(currentRepIndex + 1) <=
        thrptEstimation * m_thrptThrsh) {
      m_stableCounter++;
      if (m_stableCounter >= 3)  // [参数] 稳定3次就升，比视频的5次要快
      {
        nextRepIndex = currentRepIndex + 1;
        m_stableCounter = 0;
      }
    } else {
      m_stableCounter = 0;
    }
  }

  // [修改点4] 极低码率保护 (音频特有)
  // 如果算法想选 Index 0 (假设是 32kbps)，但缓冲还很健康 (>20秒)，
  // 强制提升到 Index 1 (假设是 96kbps)，因为 32kbps 音质太差了，能不听就不听。
  if (nextRepIndex == 0 && bufferNow > 20000000 && m_highestRepIndex >= 1) {
    nextRepIndex = 1;
  }

  answer.nextRepIndex = nextRepIndex;
  answer.decisionCase = 3;
  return answer;
}

}  // namespace ns3
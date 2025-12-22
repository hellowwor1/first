/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright 2016 Technische Universitaet Berlin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "festive.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("FestiveAlgorithm");

NS_OBJECT_ENSURE_REGISTERED(FestiveAlgorithm);

// --- 构造函数 ---
FestiveAlgorithm::FestiveAlgorithm(const videoData &videoData,
                                   const playbackData &playbackData,
                                   const bufferData &bufferData,
                                   const throughputData &throughput)
    : AdaptationAlgorithm(videoData, playbackData, bufferData, throughput),
      m_targetBuf(
          60000000),  //  [初始化] 设定目标缓冲区为 60秒 (30,000,000 微秒)
      m_delta(
          m_videoData.segmentDuration),  // [初始化] 波动范围设为一个切片的时长
      m_alpha(1.0),
      // 一开始是12 现在大幅减少
      // [初始化] 稳定性权重，数值越大，算法越不愿意切换码率
      m_highestRepIndex(videoData.averageBitrate.size() -
                        1),  // [初始化] 获取最高码率档位
      m_thrptThrsh(0.95)
// [初始化] 保守系数，只用估算带宽的 85%
// 现在激进一点，用估算带宽的95%
{
  NS_LOG_INFO(this);
  m_smooth.push_back(3);
  // [参数] 必须在当前码率稳定坚持 5 个切片，才允许尝试升级
  // 激进一点，稳定3个
  m_smooth.push_back(
      1);  // [参数] 每次升级只能升 1 个档位 (不能从 360p 直接跳到 1080p)
  NS_ASSERT_MSG(m_highestRepIndex >= 0,
                "The highest quality representation index should be => 0");
}

// --- 核心决策函数 ---
algorithmReply
FestiveAlgorithm::GetNextRep(const int64_t segmentCounter, int64_t clientId) {
  int64_t timeNow = Simulator::Now().GetMicroSeconds();
  bool decisionMade = false;  // 标记是否已经做出了“需要改变码率”的决定
  algorithmReply answer;
  answer.decisionTime = timeNow;
  answer.nextDownloadDelay = 0;  // 默认不延迟，立即下载
  answer.delayDecisionCase = 0;

  // [特殊情况] 如果是第一个切片，直接选最低码率（Index 0），快速起播
  // v2 选择中等画质
  if (segmentCounter == 0) {
    // answer.nextRepIndex = 0;
    answer.nextRepIndex = 3;
    answer.decisionCase = 0;
    return answer;
  }

  // [计算当前缓冲区]
  // 公式：当前最新缓冲 - (当前时间 - 上次传输结束时间)
  // 意为：上次下载完到现在，已经播放掉了一些，剩下的就是当前实际缓冲。
  int64_t bufferNow = m_bufferData.bufferLevelNew.back() -
                      (timeNow - m_throughput.transmissionEnd.back());

  // 缓冲区的缓冲数据有30s时，就请求下一个等级的码率
  if (bufferNow >= 30000000 && bufferNow < m_targetBuf) {
    answer.nextRepIndex++;
    answer.decisionCase = 0;
    return answer;
  }

  // [冷启动保护] 如果历史传输记录少于 20 个，数据不足以做复杂计算，保持最低码率
  // v2 选择中等画质 同时激进一点，改为 3个
  if (m_throughput.transmissionEnd.size() < 3) {
    // answer.nextRepIndex = 0;
    answer.nextRepIndex = 3;
    answer.decisionCase = 1;
    return answer;
  }

  // --- 步骤 1: 带宽估算 (使用调和平均数) ---
  std::vector<double> thrptEstimationTmp;
  // 遍历过去的历史数据（倒序遍历）
  for (unsigned sd = m_playbackData.playbackIndex.size(); sd-- > 0;) {
    if (m_throughput.bytesReceived.at(sd) == 0) {
      continue;
    } else {
      // 计算单个切片的瞬时吞吐量 = (接收比特数) / (传输耗时秒)
      thrptEstimationTmp.push_back(
          (8.0 * m_throughput.bytesReceived.at(sd)) /
          ((double)((m_throughput.transmissionEnd.at(sd) -
                     m_throughput.transmissionRequested.at(sd)) /
                    1000000.0)));
    }
    // 只取最近 20 个样本
    // 激进一点取3个
    if (thrptEstimationTmp.size() == 3) {
      break;
    }
  }

  // 计算调和平均数 (Harmonic Mean)
  // 调和平均数对“低值”敏感。如果某次网络很卡，平均值会大幅下降。这是一种保守策略。
  double harmonicMeanDenominator = 0;
  for (uint i = 0; i < thrptEstimationTmp.size(); i++) {
    harmonicMeanDenominator += 1 / (thrptEstimationTmp.at(i));
  }
  // 最终估算的带宽
  double thrptEstimation = thrptEstimationTmp.size() / harmonicMeanDenominator;

  // --- 步骤 2: 随机化缓冲区目标 (防止多用户同步震荡) ---
  // 计算一个随机的目标区间 [Target - Delta, Target + Delta]
  int64_t lowerBound = m_targetBuf - m_delta;
  int64_t upperBound = m_targetBuf + m_delta;
  // 随机生成一个 buffer 目标值
  int64_t randBuf =
      (int64_t)lowerBound + (std::rand() % (upperBound - (lowerBound) + 1));

  // [延迟下载决策] 如果当前缓冲 > 随机目标，说明缓冲太足了，休息一会
  // 休息时间 = 多出来的这部分时间
  if (bufferNow > randBuf) {
    answer.nextDownloadDelay = bufferNow - randBuf;
    answer.delayDecisionCase = 1;
  }

  // --- 步骤 3: 初步选择码率 ---
  int64_t currentRepIndex =
      m_playbackData.playbackIndex.back();  // 上一个切片的码率
  int64_t refIndex = currentRepIndex;       // 参考码率，暂定为不变

  // [降级判断 - Panic Mode]
  // 如果当前码率的比特率 > 估算带宽 * 0.85，说明网速不够了，必须降级
  if (currentRepIndex > 0 && m_videoData.averageBitrate.at(currentRepIndex) >
                                 thrptEstimation * m_thrptThrsh) {
    refIndex = currentRepIndex - 1;  // 降一级
    answer.decisionCase = 1;
    decisionMade = true;
  }

  // [升级判断 - Conservative Mode]
  assert(m_smooth.at(0) > 0);
  assert(m_smooth.at(1) == 1);
  // 如果没决定降级，且还没到最高画质，看看能不能升级
  if (currentRepIndex < m_highestRepIndex && !decisionMade) {
    // 检查最近 m_smooth.at(0) (即5) 个切片是否都保持了当前码率
    // 防止刚切上来马上又切，导致不稳定
    int count = 0;
    for (unsigned _sd = m_playbackData.playbackIndex.size() - 1; _sd-- > 0;) {
      if (currentRepIndex == m_playbackData.playbackIndex.at(_sd)) {
        count++;
        if (count >= m_smooth.at(0)) {
          break;
        }
      } else {
        break;  // 中断了，说明最近变过
      }
    }
    // 如果稳定了一段时间，且 下一档码率 <= 估算带宽，则升级
    if (count >= m_smooth.at(0) &&
        (double)m_videoData.averageBitrate.at(currentRepIndex + 1) <=
            thrptEstimation) {
      refIndex = currentRepIndex + 1;  // 升一级
      answer.decisionCase = 1;
      decisionMade = true;
    }
  }

  // 如果既不需要升级也不需要降级，直接返回维持现状
  if (!decisionMade) {
    answer.nextRepIndex = currentRepIndex;
    answer.decisionCase = 3;
    return answer;
  }

  // --- 步骤 4: 成本函数决策 (Cost Function) ---
  // 就算前面决定了要变（refIndex 变了），这里要最后算一次账，看看变合不合算。
  // 主要是防止频繁切换（Ping-Pong 效应）。

  // 计算过去一段时间内的切换次数
  int64_t numberOfSwitches = 0;
  std::vector<int64_t> foundIndices;
  // ... (遍历历史记录计算切换次数的代码逻辑)
  for (unsigned _sd = m_playbackData.playbackStart.size() - 1; _sd-- > 0;) {
    // 逻辑省略：统计 numberOfSwitches
    if (m_playbackData.playbackStart.at(_sd) < timeNow) {
      break;
    } else if (currentRepIndex != m_playbackData.playbackIndex.at(_sd)) {
      // 如果发现变化，且不是重复计数的，增加切换计数
      if (std::find(foundIndices.begin(), foundIndices.end(),
                    currentRepIndex) != foundIndices.end()) {
        continue;
      }
      numberOfSwitches++;
      foundIndices.push_back(currentRepIndex);
    }
  }

  // [计算分数]
  // 效率分 (Efficiency): 码率越高越接近带宽，分数越低（差值越小）
  double scoreEfficiencyCurrent = std::abs(
      (double)m_videoData.averageBitrate.at(currentRepIndex) /
          double(std::min(thrptEstimation,
                          (double)m_videoData.averageBitrate.at(refIndex))) -
      1.0);

  double scoreEfficiencyRef = std::abs(
      (double)m_videoData.averageBitrate.at(refIndex) /
          double(std::min(thrptEstimation,
                          (double)m_videoData.averageBitrate.at(refIndex))) -
      1.0);

  // 稳定性分 (Stability): 切换次数越多，惩罚越大（2的指数级）
  double scoreStabilityCurrent = pow(2.0, (double)numberOfSwitches);
  double scoreStabilityRef = pow(2.0, ((double)numberOfSwitches)) +
                             1.0;  // 如果切换了，次数+1，惩罚更重

  // [最终比较] Cost = Stability + alpha * Efficiency
  // 如果 (维持现状的总成本) < (切换的总成本)
  if ((scoreStabilityCurrent + m_alpha * scoreEfficiencyCurrent) <
      scoreStabilityRef + m_alpha * scoreEfficiencyRef) {
    answer.nextRepIndex =
        currentRepIndex;  // 哪怕前面想切，因为成本太高，最后决定【反悔】，保持不变
    answer.decisionCase = 4;
    return answer;
  } else {
    answer.nextRepIndex = refIndex;  // 确认切换
    return answer;
  }
}
}  // namespace ns3
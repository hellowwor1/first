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

#ifndef FESTIVE_ALGORITHM_H
#define FESTIVE_ALGORITHM_H

#include "tcp-stream-adaptation-algorithm.h"

namespace ns3 {

/**
 * \ingroup tcpStream
 * \brief Implementation of the Festive adaptation algorithm
 * 简述：Festive 自适应算法的实现类
 */
class FestiveAlgorithm : public AdaptationAlgorithm {
 public:
  // 构造函数：初始化算法所需的数据结构（视频信息、播放状态、缓冲状态、吞吐量历史）
  FestiveAlgorithm(const videoData &videoData, const playbackData &playbackData,
                   const bufferData &bufferData,
                   const throughputData &throughput);

  // 核心函数：决定下一个视频切片（Segment）下载什么码率（清晰度）
  // segmentCounter: 当前是第几个切片
  // clientId: 客户端ID
  // 返回值: algorithmReply 包含决策结果（下哪个码率，是否需要等待/休眠）
  algorithmReply GetNextRep(const int64_t segmentCounter, int64_t clientId);

 private:
  const int64_t
      m_targetBuf;  // [参数]
                    // 目标缓冲区大小（微秒），比如30秒。算法希望将缓冲区维持在这个水平。
  const int64_t m_delta;  // [参数] 缓冲区波动范围参数，用于计算随机目标。
  const double
      m_alpha;  // [参数]
                // 权重因子，用于平衡“效率（画质）”和“稳定性（切换次数）”。
  const int64_t
      m_highestRepIndex;  // [参数]
                          // 视频最高码率的索引（比如有0,1,2,3四档，这里就是3）。
  const double
      m_thrptThrsh;  // [参数]
                     // 带宽安全阈值（例如0.85），即只使用估算带宽的85%来选码率，留余量防止卡顿。
  std::vector<int>
      m_smooth;  // [参数] 平滑控制向量，控制升级的速度（不要升太快）。
};

}  // namespace ns3
#endif /* FESTIVE_ALGORITHM_H */
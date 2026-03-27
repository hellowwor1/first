#ifndef OWD_MANAGER_H
#define OWD_MANAGER_H

#include <algorithm>
#include <cmath>
#include <deque>

#include "ns3/nstime.h"
namespace ns3 {
class OwdManager {
 public:
  static OwdManager& Get() {
    static OwdManager instance;
    return instance;
  }

  void addVideoOwd(Time rxTime, int64_t owd) {
    m_videoowd.push_back({rxTime, owd});
  }
  void addAudioOwd(Time rxTime, int64_t owd) {
    m_audioowd.push_back({rxTime, owd});
  }

  std::pair<Time, int64_t> GetVideoFontOwd() { return m_videoowd.front(); }
  std::pair<Time, int64_t> GetAudioFontOwd() { return m_audioowd.front(); }

  bool DelVideoFontOwd() {
    m_videoowd.pop_front();
    return true;
  }
  bool DelAudioFontOwd() {
    m_audioowd.pop_front();
    return true;
  }

 private:
  std::deque<std::pair<Time, int64_t>> m_videoowd;
  std::deque<std::pair<Time, int64_t>> m_audioowd;
};
}  // namespace ns3
#endif  // OWD_MANAGER_H
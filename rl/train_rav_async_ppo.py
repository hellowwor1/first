import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.distributions import Categorical

from ns3gym import ns3env


# =========================================================
# 配置区
# =========================================================
OBS_DIM = 53             # 你前面定的 observation 维度
NUM_AUDIO_LEVELS = 5
NUM_VIDEO_LEVELS = 5
JOINT_ACTION_DIM = NUM_AUDIO_LEVELS * NUM_VIDEO_LEVELS

GAMMA = 0.99
GAE_LAMBDA = 0.95
CLIP_EPS = 0.2
ENTROPY_COEF = 0.01
VALUE_COEF = 0.5
LR = 1e-4

ROLLOUT_STEPS = 512
PPO_EPOCHS = 10
MINI_BATCH_SIZE = 64
TOTAL_UPDATES = 200


# =========================================================
# 模型：共享编码器 + 双 actor head + 单 critic
# =========================================================
class DualHeadActorCritic(nn.Module):
    def __init__(self, obs_dim, audio_dim, video_dim):
        super().__init__()

        # 最小化实现：先用 MLP，不上 CNN
        self.encoder = nn.Sequential(
            nn.Linear(obs_dim, 256),
            nn.ReLU(),
            nn.Linear(256, 128),
            nn.ReLU(),
        )

        self.audio_head = nn.Linear(128, audio_dim)
        self.video_head = nn.Linear(128, video_dim)
        self.value_head = nn.Linear(128, 1)

    def forward(self, obs):
        """
        obs: [B, obs_dim]
        """
        feat = self.encoder(obs)
        audio_logits = self.audio_head(feat)
        video_logits = self.video_head(feat)
        value = self.value_head(feat).squeeze(-1)
        return audio_logits, video_logits, value


# =========================================================
# rollout buffer
# =========================================================
class RolloutBuffer:
    def __init__(self):
        self.obs = []
        self.audio_actions = []
        self.video_actions = []
        self.joint_actions = []
        self.active_heads = []      # 0: audio event, 1: video event
        self.logprobs = []          # 只保存当前 active head 的 logprob
        self.values = []
        self.rewards = []
        self.dones = []

    def clear(self):
        self.__init__()


# =========================================================
# 工具函数
# =========================================================
def encode_joint_action(audio_action, video_action):
    """
    joint_action = audio * 5 + video
    """
    return int(audio_action) * NUM_VIDEO_LEVELS + int(video_action)


def decode_joint_action(joint_action):
    audio_action = joint_action // NUM_VIDEO_LEVELS
    video_action = joint_action % NUM_VIDEO_LEVELS
    return audio_action, video_action


def compute_gae(rewards, values, dones, last_value, gamma=0.99, lam=0.95):
    """
    计算 GAE advantage 和 return
    """
    advantages = []
    gae = 0.0

    values = values + [last_value]

    for t in reversed(range(len(rewards))):
        non_terminal = 1.0 - dones[t]
        delta = rewards[t] + gamma * values[t + 1] * non_terminal - values[t]
        gae = delta + gamma * lam * non_terminal * gae
        advantages.insert(0, gae)

    returns = [adv + v for adv, v in zip(advantages, values[:-1])]
    return advantages, returns


# =========================================================
# 按当前事件选择动作
# =========================================================
@torch.no_grad()
def select_action(model, obs_np, device):
    """
    obs[0] 约定为 event_type:
        0 -> AUDIO_EVENT
        1 -> VIDEO_EVENT

    我们仍然采样 audio/video 两个动作，组成 joint action 发给 ns3-gym，
    但训练时只对 active head 的 logprob 反传。
    """
    obs = torch.tensor(obs_np, dtype=torch.float32, device=device).unsqueeze(0)
    audio_logits, video_logits, value = model(obs)

    audio_dist = Categorical(logits=audio_logits)
    video_dist = Categorical(logits=video_logits)

    audio_action = audio_dist.sample()
    video_action = video_dist.sample()

    # 当前 step 到底是音频事件还是视频事件
    event_type = int(obs_np[0])

    if event_type == 0:
        active_logprob = audio_dist.log_prob(audio_action)
    else:
        active_logprob = video_dist.log_prob(video_action)

    joint_action = encode_joint_action(audio_action.item(), video_action.item())

    return {
        "audio_action": audio_action.item(),
        "video_action": video_action.item(),
        "joint_action": joint_action,
        "active_head": event_type,
        "logprob": active_logprob.item(),
        "value": value.item(),
    }


# =========================================================
# PPO 更新
# =========================================================
def ppo_update(model, optimizer, buffer, last_value, device):
    advantages, returns = compute_gae(
        rewards=buffer.rewards,
        values=buffer.values,
        dones=buffer.dones,
        last_value=last_value,
        gamma=GAMMA,
        lam=GAE_LAMBDA,
    )

    obs = torch.tensor(np.array(buffer.obs), dtype=torch.float32, device=device)
    audio_actions = torch.tensor(buffer.audio_actions, dtype=torch.long, device=device)
    video_actions = torch.tensor(buffer.video_actions, dtype=torch.long, device=device)
    active_heads = torch.tensor(buffer.active_heads, dtype=torch.long, device=device)
    old_logprobs = torch.tensor(buffer.logprobs, dtype=torch.float32, device=device)
    returns = torch.tensor(returns, dtype=torch.float32, device=device)
    advantages = torch.tensor(advantages, dtype=torch.float32, device=device)

    # 标准化 advantage
    advantages = (advantages - advantages.mean()) / (advantages.std() + 1e-8)

    n = obs.size(0)
    indices = np.arange(n)

    for _ in range(PPO_EPOCHS):
        np.random.shuffle(indices)

        for start in range(0, n, MINI_BATCH_SIZE):
            end = start + MINI_BATCH_SIZE
            batch_idx = indices[start:end]

            batch_obs = obs[batch_idx]
            batch_audio_actions = audio_actions[batch_idx]
            batch_video_actions = video_actions[batch_idx]
            batch_active_heads = active_heads[batch_idx]
            batch_old_logprobs = old_logprobs[batch_idx]
            batch_returns = returns[batch_idx]
            batch_advantages = advantages[batch_idx]

            audio_logits, video_logits, values = model(batch_obs)

            audio_dist = Categorical(logits=audio_logits)
            video_dist = Categorical(logits=video_logits)

            # 分别算两个 head 的 logprob
            audio_logprob = audio_dist.log_prob(batch_audio_actions)
            video_logprob = video_dist.log_prob(batch_video_actions)

            # 当前样本只选 active head 的 logprob
            selected_logprob = torch.where(
                batch_active_heads == 0, audio_logprob, video_logprob
            )

            # 同理，只用 active head 的 entropy
            audio_entropy = audio_dist.entropy()
            video_entropy = video_dist.entropy()
            selected_entropy = torch.where(
                batch_active_heads == 0, audio_entropy, video_entropy
            )

            ratio = torch.exp(selected_logprob - batch_old_logprobs)

            surr1 = ratio * batch_advantages
            surr2 = torch.clamp(ratio, 1.0 - CLIP_EPS, 1.0 + CLIP_EPS) * batch_advantages

            policy_loss = -torch.min(surr1, surr2).mean()
            value_loss = ((values - batch_returns) ** 2).mean()
            entropy_loss = selected_entropy.mean()

            loss = policy_loss + VALUE_COEF * value_loss - ENTROPY_COEF * entropy_loss

            optimizer.zero_grad()
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            optimizer.step()


# =========================================================
# 主训练循环
# =========================================================
def train():
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    # 注意：
    # startSim=False 表示你需要手动先在终端运行 ./waf --run "opengym"
    env = ns3env.Ns3Env(port=5555, startSim=False)

    model = DualHeadActorCritic(
        obs_dim=OBS_DIM,
        audio_dim=NUM_AUDIO_LEVELS,
        video_dim=NUM_VIDEO_LEVELS,
    ).to(device)

    optimizer = optim.Adam(model.parameters(), lr=LR)

    obs = env.reset()
    buffer = RolloutBuffer()

    for update_idx in range(TOTAL_UPDATES):
        for step_idx in range(ROLLOUT_STEPS):
            act = select_action(model, obs, device)

            next_obs, reward, done, info = env.step(act["joint_action"])

            buffer.obs.append(obs)
            buffer.audio_actions.append(act["audio_action"])
            buffer.video_actions.append(act["video_action"])
            buffer.joint_actions.append(act["joint_action"])
            buffer.active_heads.append(act["active_head"])
            buffer.logprobs.append(act["logprob"])
            buffer.values.append(act["value"])
            buffer.rewards.append(float(reward))
            buffer.dones.append(float(done))

            obs = next_obs

            if done:
                obs = env.reset()

        # rollout 末尾 bootstrap
        with torch.no_grad():
            obs_tensor = torch.tensor(obs, dtype=torch.float32, device=device).unsqueeze(0)
            _, _, last_value_tensor = model(obs_tensor)
            last_value = last_value_tensor.item()

        ppo_update(model, optimizer, buffer, last_value, device)
        buffer.clear()

        print(f"[Update {update_idx+1}/{TOTAL_UPDATES}] done")

    env.close()


if __name__ == "__main__":
    train()
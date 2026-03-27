from ns3gym import ns3env

env = ns3env.Ns3Env(port=5555, startSim=False)

obs = env.reset()
print("Initial obs:", obs)

for i in range(10):
    action = env.action_space.sample()
    obs, reward, done, info = env.step(action)
    print("Step:", i, "Reward:", reward)

env.close()

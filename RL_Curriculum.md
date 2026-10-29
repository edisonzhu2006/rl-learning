# RL Review Curriculum: Grand Prix

*Created: 2026-10-27, 15:16 EDT. Revised 2026-10-27: switched to driving, using existing sims, in two stages (2D track, then 3D).*

**Who this is for:** someone who already knows the RL concepts and wants to review them by building.
**Time:** about 11 weeks at 6–8 hrs/week. Go faster through anything you can already derive from memory.

---

## 1. The Game: Two Stages

**Stage 1, The Track (2D, Weeks 1–6).** Learn the core algorithms on a fast, well-benchmarked top-down racer. The goal is to get each algorithm right where bugs are cheap to find.

**Stage 2, Going 3D (Weeks 7–11).** Take your agents into 3D simulators with first-person cameras, realistic physics, procedurally generated roads and traffic. The goal is to learn what breaks when the environment gets realistic: partial observability, generalization, other agents, and rewards you can't hand-write.

| Stage | Sim | Why this one |
|---|---|---|
| 1 | [Sutton & Barto Racetrack](https://kngwyu.github.io/rlog/en/2022/03/07/sandb-exercise-racetrack.html) (Phase 1 only) | Tabular, with an exact solution from value iteration. Existing implementation: [ondrejbiza/racetrack](https://github.com/ondrejbiza/racetrack) |
| 1 | **[Gymnasium CarRacing-v3](https://gymnasium.farama.org/environments/box2d/car_racing/)** | Top-down 96×96 pixels, random tracks, **both discrete (5) and continuous (3) actions**, and a well-known score target (900 = solved) |
| 2 | **[Donkey Car sim](https://docs.donkeycar.com/guide/deep_learning/simulator/)** ([gym-donkeycar](https://github.com/tawnkramer/gym-donkeycar/releases)) | 3D Unity racing tracks with a first-person camera. **Ships a native Mac build**, can run up to 4× faster than real time, and you can drive it yourself |
| 2 | **[MetaDrive](https://metadriverse.github.io/metadrive/)** | 3D roads that are procedurally generated with train/test splits, traffic, multi-agent scenarios with 20–40 cars, and lidar, RGB or top-down observations ([paper](https://arxiv.org/abs/2109.12674)) |

### Mac notes
- **MetaDrive:** install from GitHub `main`, not PyPI. The 0.4.3 PyPI release has rendering issues on Apple Silicon and doesn't support Python 3.12 ([install docs](https://github.com/metadriverse/metadrive/blob/main/documentation/source/install.rst)):
  `pip install "metadrive-simulator @ git+https://github.com/metadriverse/metadrive.git@main"`
- **Donkey sim:** download `DonkeySimMac.zip` from the [releases page](https://github.com/tawnkramer/gym-donkeycar/releases), run `chmod +x`, and point gym-donkeycar to `donkey_sim.app/Contents/MacOS/donkey_sim`.
- **Not recommended here:**
  - CARLA has no official macOS support. There are community builds, but one recommends 32 GB+ of RAM, and another route runs CARLA through Wine and needs about 45 GB of disk ([carla-macos](https://github.com/mkhaled87/carla-macos), [guide](https://github.com/nathanaday/carla-mac-os)).
  - TrackMania RL ([tmrl](https://github.com/trackmania-rl/tmrl)) needs Windows or Linux and an NVIDIA GPU. It's worth trying later if you have a PC.

### Scoring (used in every boss fight)
| Stage | Metric |
|---|---|
| 1 | CarRacing mean score over 100 episodes (−0.1 per frame, +1000/N per track tile visited) |
| 2, Donkey | Clean laps in a row, lap time, cross-track error, smoothness (steering jerk) |
| 2, MetaDrive | Success rate on **held-out** maps, route completion, collision rate |

*Boss targets are rough. If a threshold turns out to be clearly too easy or too hard after a serious attempt, adjust it and write down why.*

---

## 2. Resource Stack (kept small on purpose)

You don't need everything here. **Bold** items are the spine; the rest are references for when you get stuck.

### Lectures
- **[Pieter Abbeel, Foundations of Deep RL (6 lectures)](https://x.com/pabbeel/status/1430425271199404034).** The fastest good review. Links: [L1 MDPs](https://www.youtube.com/watch?v=2GwBez0D20A), [L2 DQN](https://www.youtube.com/watch?v=Psrhxy88zww), [L3 PG & GAE](https://www.youtube.com/watch?v=AKbX1Zvo7r8), L4 TRPO/PPO (in the playlist), [L5 DDPG/SAC](https://www.youtube.com/watch?v=pg-lKy7JIRk), [L6 Model-based](https://www.youtube.com/watch?v=2o1yrkbpcUk).
- **[Berkeley CS285, Spring 2026](https://rail.eecs.berkeley.edu/deeprlcourse/)** (Levine). Use this for depth on imitation learning, model-based RL, offline RL and exploration. Recorded lectures: [Fall 2023 playlist](https://www.youtube.com/playlist?list=PL_iWQOsE6TfVYGEGiAOMaOzzv41Jfm_Ps).
- [David Silver, UCL RL (2015)](https://davidstarsilver.wordpress.com/teaching/). Only needed if the tabular fundamentals feel rusty.

### Text
- **[Sutton & Barto, 2nd ed.](http://incompleteideas.net/book/the-book-2nd.html)** (free). Ch. 3–6 for Phase 1, Ch. 9–13 for function approximation.
- **[Murphy, *Reinforcement Learning: An Overview*](https://arxiv.org/abs/2412.05265)** (free, updated Dec 2025). A modern reference for model-based RL, offline RL and MARL.
- [Lambert, *The RLHF Book*](https://rlhfbook.com/) (free online). For Phase 11.

### Code
- **[CleanRL](https://docs.cleanrl.dev/)**. Single-file reference implementations. The Atari-style CNN variants (`dqn_atari.py`, `ppo_atari.py`) adapt directly to CarRacing.
- **[The 37 Implementation Details of PPO](https://iclr-blog-track.github.io/2022/03/25/ppo-implementation-details/)**.
- [OpenAI Spinning Up](https://spinningup.openai.com/en/latest/). PG, DDPG, TD3 and SAC derivations.
- [ARENA Chapter 2](https://learn.arena.education/chapter2_rl/05_mcts_alphazero/). Guided DQN and PPO exercises to use as an answer key.

### Driving-specific
- **3D racing from pixels:** [Raffin, Learning to Drive Smoothly in Minutes](https://araffin.github.io/project/learning-to-drive/). SAC on a frozen VAE in the Donkey sim, giving a smooth controller in about 5–10 minutes ([code](https://github.com/araffin/learning-to-drive-in-5-minutes); [newer SB3 version](https://github.com/araffin/aae-train-donkeycar/releases/tag/live-twitch-2)).
- **World models:** [Ha & Schmidhuber, World Models](https://worldmodels.github.io/) (the first agent to solve CarRacing), [DreamerV3](https://arxiv.org/abs/2301.04104).
- **Imitation and rewards:** [DAgger, Ross et al. 2011](https://arxiv.org/abs/1011.0686), [Christiano et al. 2017, Deep RL from Human Preferences](https://arxiv.org/abs/1706.03741).
- **Optional model-based detour:** [HighwayEnv](https://highway-env.farama.org/) `parking-v0`. Its quickstart includes a model-based RL Colab.
- **Inspiration:** [GT Sophy, Nature 2022](https://www.cs.utexas.edu/~pstone/Papers/bib2html/b2hd-nature22.html), and [Learning to Drive in a Day](https://arxiv.org/abs/1807.00412) (Wayve: DDPG on a real car).

---

## 3. How to Learn (the method)

Since you're reviewing rather than learning from scratch, use **retrieval first, reading second**. Each phase runs in this loop:

1. **Cold derivation (30 min).** Before watching or reading anything, write the phase's core math on paper from memory. Mark what you couldn't reproduce.
2. **Targeted review.** Watch or read only what covers the gaps from step 1, at 1.5x.
3. **Implement from scratch.** In Stage 1, write every algorithm yourself; that's where the review happens. In Stage 2, you can start from your Stage 1 code or from SB3/CleanRL, since the focus moves to the environment problems.
4. **Diff against the reference** (CleanRL, ARENA, Spinning Up). Write down every difference and whether it matters.
5. **Boss fight.** Run 3+ seeds, log to TensorBoard or W&B, report mean ± std, and **save a GIF or video of the best run.**
6. **One-page writeup** in `notes/`: what the algorithm does, what broke, one plot, and answers to the self-check questions.

**Debugging ladder (pixel-based driving).** When something doesn't learn, go down this list in order:
1. Look at the observations the agent actually gets, after preprocessing and frame stacking.
2. Does a random agent score what you expect (CarRacing random play is around −50 to −100)?
3. Is the frame-skip/action-repeat what you think it is?
4. Are the action scale and order right? The discrete CarRacing steering indices differ between versions, so check yours.
5. Does it learn when you freeze the encoder (a pretrained VAE or random CNN)?
6. Is the discount horizon long enough to "see" the next corner?

---

## 4. Stage 1: The Track (2D)

### Phase 1: Tabular Racetrack (Week 1)
- **Review:** MDPs, Bellman equations, policy and value iteration, MC control (on-policy, and off-policy with importance sampling), SARSA, Q-learning. Sources: S&B Ch. 3–6, Abbeel L1.
- **Build** on S&B Exercise 5.12. State = (cell, velocity), there are 9 actions, and with probability 0.1 the action does nothing.
  - Value iteration. This gives the exact optimum.
  - Off-policy MC control. Cap episode length, since a crash resets the car and episodes can otherwise run forever.
  - Q-learning.
- **Boss:** from every start cell, the greedy MC and Q-learning policies take ≤ 5% more steps than the value-iteration optimum.
- **Self-check:** Why does weighted importance sampling have lower variance than ordinary importance sampling? What breaks in MC control when episodes are very long?

### Phase 2: DQN from Pixels (Week 2)
- **Review:** the deadly triad, target networks, replay buffers, Double DQN, Dueling networks, n-step returns, frame stacking. Sources: Abbeel L2, CleanRL `dqn_atari.py`.
- **Build:** DQN, then Double DQN, on CarRacing-v3 with `continuous=False`. Use grayscale, frame stacking and frame skip.
- **Boss:** mean score ≥ 700.
- **Self-check:** Where does maximization bias show up here? Why does the car "wobble" with discrete actions?

### Phase 3: Policy Gradients & Actor-Critic (Week 3)
- **Review:** derive the PG theorem, REINFORCE, baselines, A2C, GAE(λ), Gaussian or Beta policies, and tanh squashing with its log-prob correction. Sources: Abbeel L3, Spinning Up.
- **Build:** A2C with GAE on the **continuous** action space. Try a Beta distribution for the bounded gas and brake actions.
- **Boss:** mean score ≥ 600, with smoother steering than DQN. Plot both agents' steering traces.
- **Self-check:** What is λ trading off? Why can't you just clip a Gaussian action to [−1, 1] without fixing the log-prob?

### Phase 4: PPO (Week 4)
- **Review:** trust regions, the TRPO to PPO-clip transition, then the full *37 Implementation Details*. Source: Abbeel L4.
- **Build:** PPO with vectorized envs. **Ablate 3 details** (observation normalization, advantage normalization, learning-rate annealing) and record what each changes.
- **Boss:** mean score ≥ 850, and the ablation table is written up.
- **Self-check:** What does the clip prevent, and when does it fail to keep the new policy close to the old one?

### Phase 5: Off-Policy Continuous Control (Week 5)
- **Review:** DDPG → TD3 (clipped double-Q, target smoothing, delayed actor updates) → SAC (max-entropy objective, automatic temperature tuning). Sources: Abbeel L5, Spinning Up, CleanRL `sac_continuous_action.py`.
- **Build:** SAC from pixels. Run it once with the CNN trained end to end, and once on a **frozen encoder**: a VAE trained on random-play frames, which previews Phase 7.
- **Boss:** reaches PPO's score using fewer environment steps. Plot score against steps for both.
- **Self-check:** Which of TD3's tricks fixes which failure of DDPG? Why does a frozen encoder make off-policy learning from pixels so much easier?

### Phase 6: World Models (Week 6)
- **Review:** VAEs, latent dynamics (MDN-RNN, RSSM), planning and learning "in imagination". Sources: [World Models](https://worldmodels.github.io/), [DreamerV3](https://arxiv.org/abs/2301.04104), Abbeel L6.
- **Build:** World Models on CarRacing: a VAE (V), an MDN-RNN (M), and a tiny controller (C) trained with CMA-ES or PPO. *Optional:* run DreamerV3's reference code and compare. *Optional detour:* PETS-style MPC with a learned model ensemble on HighwayEnv `parking-v0`.
- **Boss:** mean score ≥ 850 with V+M+C. *Stretch goal:* ≥ 900 ("solved").
- **Self-check:** Why did V+M beat V alone on sharp corners? What does Dreamer change compared with the 2018 setup?

---

## 5. Stage 2: Going 3D

### Phase 7: Port to 3D (Week 7)
- **Review:** partial observability from a first-person camera, sample efficiency in real time, and why state representation matters. Source: [Raffin's write-up](https://araffin.github.io/project/learning-to-drive/).
- **Build** in the Donkey sim:
  1. Bring your Phase 5 SAC over on raw pixels and note what breaks.
  2. Reproduce SAC on a frozen VAE, using frames you record yourself in teleoperation.
  3. Shape the reward for smoothness: penalize steering change and reward speed along the track.
- **Boss:** 10 clean laps in a row on one track, with a smooth controller trained in under 30 minutes of simulated time.
- **Self-check:** What's different about the observation distribution in first-person 3D compared with the top-down 2D view? Why does the VAE trick work here?

### Phase 8: Imitation & Offline RL (Week 8)
- **Review:** behavioral cloning, compounding error, DAgger, distribution shift in offline RL, CQL and IQL. Sources: CS285 imitation and offline RL lectures, [DAgger](https://arxiv.org/abs/1011.0686).
- **Build** in the Donkey sim:
  1. Drive about 10 laps yourself (keyboard or gamepad) and train **BC**. Watch it drift, which is compounding error you can see.
  2. **DAgger**, with your Phase 7 SAC agent labeling the states the BC policy actually visits.
  3. Offline RL (IQL or CQL) on a mixed log: your laps, noisy SAC rollouts, and early checkpoints. No environment interaction allowed.
- **Boss:** DAgger completes laps that BC can't, and offline RL beats BC trained on the same log.
- **Self-check:** Why does BC's error grow quadratically with the horizon? Why does naive offline Q-learning overestimate values?

### Phase 9: Generalization (Week 9)
- **Review:** overfitting in RL, procedural generation, domain randomization, train/test splits. Sources: [MetaDrive paper](https://arxiv.org/abs/2109.12674), Murphy.
- **Build** in MetaDrive: train PPO or SAC on 1, 10, 100 and 1,000 procedurally generated maps, and evaluate each on held-out maps. Then add domain randomization (traffic density, friction, lighting).
- **Boss:** ≥ 70% success rate on held-out maps. Plot the generalization gap against the number of training maps; the paper found that more training diversity improves test performance, so check whether you see the same.
- **Self-check:** Why can an agent hit 100% on training maps and fail on test maps? What is domain randomization approximating?

### Phase 10: Multi-Agent Traffic (Week 10)
- **Review:** non-stationarity, independent learners vs. parameter sharing, centralized critics (MAPPO), self-play, and the "sportsmanship" problem from [GT Sophy](https://www.cs.utexas.edu/~pstone/Papers/bib2html/b2hd-nature22.html). Source: Murphy's MARL chapter.
- **Build** on MetaDrive's multi-agent scenarios (e.g. `MultiAgentIntersectionEnv`, roundabout):
  1. Independent PPO.
  2. Parameter-shared PPO with a centralized critic. Compare the two.
- **Boss:** shared-policy agents reach ≥ 70% success with a lower collision rate than independent PPO.
- **Self-check:** Why does independent learning struggle here? How do you reward overtaking or merging without encouraging aggression?

### Phase 11: Learning the Reward from Preferences (Week 11)
- **Review:** reward modeling, the Bradley-Terry model, the RLHF pipeline, and how this connects to LLM post-training (PPO and GRPO for LMs, DPO). Sources: [Christiano et al. 2017](https://arxiv.org/abs/1706.03741), [The RLHF Book](https://rlhfbook.com/).
- **Build** in Donkey or MetaDrive: the goal is "drive like a good human driver", with no hand-written comfort term. Watch pairs of 2-second clips, click the one you prefer, train a reward model, then optimize it with SAC.
- **Boss:** with ≤ 500 labels, the agent has lower jerk than the Phase 7 agent and no more than 10% slower lap times.
- **Self-check:** What does reward hacking look like here? How would a KL penalty to a reference policy help, and how is that the same idea as in LLM RLHF?

### Optional extensions
- **Sim-to-real:** the Donkey sim models a real, cheap RC car ([Donkey Car docs](https://docs.donkeycar.com/)). Your Phase 7 pipeline was designed to transfer.
- **TrackMania** on a Windows or Linux machine with an NVIDIA GPU, via [tmrl](https://github.com/trackmania-rl/tmrl).

---

## 6. Suggested Repo Layout

```
RL Learning/
├── RL_Curriculum.md        # this file
├── common/                 # shared wrappers (preprocessing, frame stack), eval harness, GIF/video recording
├── stage1_track/           # one folder per phase: 01_tabular/, 02_dqn/, 03_pg/, 04_ppo/, 05_sac/, 06_world_models/
├── stage2_3d/              # 07_donkey_port/, 08_imitation/, 09_generalization/, 10_marl/, 11_preferences/
├── data/                   # teleop laps, offline logs, preference labels, VAE training frames
├── checkpoints/
├── runs/                   # TensorBoard / W&B logs
├── media/                  # best-run GIFs/videos per phase
└── notes/                  # one-page writeup per phase
```

---

## 7. Progress Tracker

| Phase | Stage | Topic | Boss fight | Status |
|---|---|---|---|---|
| 1 | Track | Tabular Racetrack | Within 5% of VI optimum | ☐ |
| 2 | Track | DQN | CarRacing ≥700 | ☐ |
| 3 | Track | A2C / GAE | ≥600, smoother than DQN | ☐ |
| 4 | Track | PPO | ≥850, plus ablations | ☐ |
| 5 | Track | SAC / TD3 | PPO score in fewer steps | ☐ |
| 6 | Track | World models | ≥850 (stretch 900) | ☐ |
| 7 | 3D | Port to Donkey | 10 clean laps, under 30 min of training | ☐ |
| 8 | 3D | Imitation & offline | DAgger fixes BC, offline RL beats BC | ☐ |
| 9 | 3D | Generalization | ≥70% on held-out maps | ☐ |
| 10 | 3D | Multi-agent | ≥70% success, fewer collisions | ☐ |
| 11 | 3D | Preference rewards | Smoother at ≤10% slower, ≤500 labels | ☐ |

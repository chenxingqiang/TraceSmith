# **GPU Profiling 与调用栈采集（Callstack）方向的开源项目**## **1. NVIDIA Nsight 系列的开源组件**虽然 Nsight 本体闭源，但其 profiling 底层依赖部分 **开源的 CUPTI（CUDA Profiling Tools Interface）**。### **➡️ CUPTI（CUDA Profiling Tools Interface）**GitHub：
https://github.com/NVIDIA/cupti-samples你可直接借用：
* GPU kernel launch hook
* GPU instruction-level profiling
* Context / Stream tracking
* Callback API：可收集 kernel 级别执行指令、时间戳、调用关系
* Activity API：收集 GPU pipeline 所有事件
👉 **CUPTI 的 Callback API + Activity API**完全适合作为你题目中“收集 1 万条 GPU 指令调用栈”的基础。------## **2. ROCm Open-Source Profiler (适合 AMD GPU)**AMD 的 **ROC Profiler** 完全开源，可学习其数据收集与回放体系。GitHub:
https://github.com/ROCm/rocprofiler核心价值：* 完整的 GPU Event Trace 收集框架
* 支持指令级 profiling
* 支持多 GPU、异步 event 流
* Profiling + Replay 机制------
## **3. Intel GPU Open Source Profiler (VTune 相关组件开源)**Intel GPU profiling 的一部分接口是开源的。GitHub:
https://github.com/intel/compute-runtime学习点：
* OS 层 hook GPU 调度
* Kernel tracing
* 调用路径构建------## **4. Google Perfetto（超强的 Trace / Callstack 采集框架）**Google Chrome 团队打造的一个 **跨平台 Trace 收集系统**，支持 GPU + CPU + 用户态调用栈。
GitHub：
https://github.com/google/perfetto亮点：
* 高性能 trace buffer（支持百万事件/sec）
* 原生支持 GPU/CPU 事件
* 易扩展事件格式（proto 转换）
* 可嵌入自定义 profilers👉 可用 Perfetto 当你的 **统一 trace 管线 + 可视化工具**。------## **5. Vulkan / GPUOpen - Radeon GPU Profiler (RGP) - 开源组件**GitHub:
https://github.com/GPUOpen-Tools内容包括：* GPU instruction timeline
* 线程调度可视化
* CommandBuffer trace
* Shader 调用路径（可类比调用栈）其中 RGP 的数据格式完全公开，可参考其 **GPU 指令执行序列格式**。------
# **✅ 二、GPU 调用栈采集（StackTrace）相关开源框架**## **6. LLVM XRay**Google 与 LLVM 合作的 **低开销函数调用跟踪工具**，可用于 GPU kernel wrapper 层。GitHub：https://github.com/llvm/llvm-project/tree/main/compiler-rt/lib/xray特点：* 低侵入函数入口/退出 hook
* 超轻量级 ring-buffer 记录
* 二进制级 instrumentation👉 可直接借鉴来实现 “调用栈采集不影响 GPU 执行状态”。------## **7. eBPF + GPU 驱动 Hook 技术（开源 BCC/BPFTrace 框架）**结合 eBPF 可以实现：* 系统调用级 GPU 调用链捕获
* 进程 GPU 调度监控
* ring buffer 高速事件采集相关项目：* https://github.com/iovisor/bcc
* https://github.com/iovisor/bpftrace👉 可通过 eBPF attach GPU 驱动函数，获取 GPU 调度链路，适合“OS-level 的 Stack 收集”。------# **✅ 三、Replay（回放机制）设计可参考的开源项目**## **8. RenderDoc（完全开源的 GPU 调试器）**GitHub：https://github.com/baldurk/renderdoc核心价值点：* 完整的 GPU 命令流 Capture/Replay 架构
* 跨平台支持
* 支持 frame replay、buffer replay、shader replayRenderDoc 架构非常适合作为 **GPU Replay 系统参考**。------## **9. NVIDIA Nsight / CUDA Trace Replay（机制文档公开）**虽然源代码不开放，但其 **Trace 活动格式、Replay 原理文档是开放的**。可参考：* Event Record Model
* GPU timeline serialization
* Replay consistency model非常适合作为你题目中的“GPU 状态机恢复”参考。------## **10. Mesa GPU Driver（OpenGL/Vulkan 驱动）**Mesa GPU 驱动内部带有：* GPU CommandBuffer trace
* 局部 replay 流
* Shader 序列记录Mesa 侧的 Trace/Replay 设计可直接学习。------# **✅ 四、框架级 Profiling 系统（可提供未来架构参考）**## **11. PyTorch Profiler**GitHub：https://github.com/pytorch/kinetoKineto 可作为：* GPU Kernel Timeline profiler
* 支持 CUDA Trace、NCCL Trace
* 有 profiling buffer 与 schema适合作为 “数据采集 → 统一格式 → 回放” 的 pipeline 参考。------## **12. TensorFlow Profiler / XLA HLO Tracing**GitHub：https://github.com/tensorflow/tensorflow/tree/master/tensorflow/core/profiler亮点：* 分布式 tracing
* GPU kernel timeline
* 内存状态机回放------# **✅ 五、你可以参考的最佳组合方案（建议）**### **✔ 建议的 “方案架构参考”：**| **功能**                      | **推荐开源项目**                    |
| ----------------------------- | ----------------------------------- |
| **GPU 指令级 Trace**          | CUPTI / ROCm ROC Profiler           |
| **调用栈采集（Stacktrace）**  | LLVM XRay + Perfetto                |
| **高性能 Trace 存储**         | Perfetto Trace Buffer               |
| **Replay（回放 GPU 状态机）** | RenderDoc / ROCm Replay 机制        |
| **多流、多 GPU 支持**         | PyTorch Kineto、TensorFlow Profiler |这套组合基本能实现笔试题的要求：### **要求匹配**| **笔试题要求**           | **对应开源技术**              |
| ------------------------ | ----------------------------- |
| 1 万+ GPU 指令调用栈采集 | CUPTI Activity + XRay         |
| 不影响 GPU 执行状态      | ring-buffer + async write     |
| 回放机制（多流）         | RenderDoc / ROCm Replay       |
| 文档、设计思路           | Perfetto + RenderDoc 架构文档 |------
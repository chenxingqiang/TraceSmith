# **《TraceSmith：GPU Profiling & Replay 系统》目标规划书**

## **📍**
## **一、项目概述**
**TraceSmith** 致力于构建一套跨平台、轻量、高性能的 GPU Profiling 工具，支持：* 在不中断业务的情况下采集 **1 万+ 指令级 GPU 调用栈**

* 构建可序列化的 GPU 执行轨迹
* 基于事件流恢复 **GPU 状态机**（State Machine）
* 提供 GPU 指令流 **Replay / 回放能力**
* 支持多 GPU、多 Stream、异步执行模型该项目面向 AI 编译器、深度学习框架、GPU 驱动工程师和超算平台研发人员。

# **二、项目目标（Goal）**

## **总目标：**
开发一款开源 GPU 高级 Profiling 工具，具备 **Real-time profiling + Deterministic replay + State reconstruction** 功能，成为国产 GPU、AI 框架、算子优化工具链中的关键组件。

# **三、阶段性目标（Milestones）**

## **Phase 1：可运行的最小版本（MVP）**
周期：4–6 周
### **目标：**
  * 支持采集 GPU Kernel 执行事件* 基于 CUPTI / ROCprofiler 获取：
  * kernel launch events
  * stream ID
  * start/end timestamp
  * 设计 Trace Record 结构与二进制格式（TraceSmith Trace v0.1）
  * 开发 CLI 工具：  * TraceSmith record（记录事件流）
  * TraceSmith view（文本化显示）


  ### **交付物：**
    * 事件采集模块（record）
    * Trace 存储格式（sbt: TraceSmith Binary Trace）
    * MVP 文档 + 样例

## **Phase 2：指令级调用栈采集（核心功能）**

周期：5–8 周

### **目标：**
  * 使用 LLVM XRay/eBPF 采集 GPU kernel 的 host-side 调用栈
  * GPU kernel 内部之间的 “调用链” 与执行依赖图
  * 自动构建 GPU 指令流（Instruction Stream）
  * 高性能 ring-buffer，确保低开销运行

### **交付物：**
  * 调用栈采集引擎（stack collector）
  * Instruction Stream Builder
  * 状态机初步实现（State Machine v0.1）

## **Phase 3：GPU 状态机恢复与 Timeline 构建**
周期：6–8 周

### **目标：**
  * 根据 Trace 事件重建 GPU 执行状态机：
  * Idle → Dispatch Pending → Running → Wait → Finished
  * 多流（Multi-stream）调度重建
  * 多 GPU（Multi-device）事件对齐* 生成可视化 Timeline（配合 Perfetto / 自制 UI）

### **交付物**
* 状态机恢复（State Rebuilder）
* 图形化 Timeline Viewer（可选使用前端 React + ECharts）
* 跨 GPU 时间戳同步机制

## **Phase 4：Replay（回放）引擎开发**
周期：8–10 周


### **目标：**

* 将采集的指令流序列化
* 基于 Command Stream 进行 GPU 指令 Replay
* 支持：
  * 单流回放
  * 多流回放
  * 部分片段回放（Partial Replay）
  * 回放一致性检查（Deterministic Check）

### **交付物：**
* Replay Engine v1.0
* 流调度仿真器（Stream Scheduler Emulator）
* 可复现性验证框架

## **Phase 5：工程化、文档、可视化、发布**

周期：4 周

### **目标：*** 完整 API 文档
* 图形界面的可视化工具（TraceSmith Studio）
* Python binding / Rust binding
* Docker 镜像、Homebrew 发布
* 发布 v1.0 开源版本

# **🧱** # **四、系统架构（高层）**
```
 ┌──────────────────────────────────────┐
 │               TraceSmith             │
 ├──────────────────────────────────────┤
 │ 1. Data Capture Layer                │
 │    - CUPTI / ROCm hooks              │
 │    - eBPF / XRay instrumentation     │
 │    - Ring Buffer (Lock-free)         │
 ├──────────────────────────────────────┤
 │ 2. Trace Format Layer                │
 │    - SBT (TraceSmith Binary Trace)   │
 │    - Event Encoding / Compression    │
 ├──────────────────────────────────────┤
 │ 3. State Reconstruction              │
 │    - GPU Timeline Builder            │
 │    - Stream Dependency Graph         │
 │    - State Machine Generator         │
 ├──────────────────────────────────────┤
 │ 4. Replay Engine                     │
 │    - Instruction Replay              │
 │    - Stream Re-Scheduler             │
 │    - Deterministic Checker           │
 ├──────────────────────────────────────┤
 │ 5. UI & Visualization                │
 │    - CLI Tools                       │
 │    - Perfetto Integration            │
 │    - TraceSmith Studio (optional)    │
 └──────────────────────────────────────┘
```
------

# **⚙️** # **五、技术路线**
| **模块**    | **技术**                                 |
| ----------- | ---------------------------------------- |
| GPU Trace   | CUPTI, ROCm rocprofiler, Vulkan Trace    |
| 调用栈采集  | eBPF, LLVM XRay                          |
| 数据格式    | Protobuf / Flatbuffers / 自研 SBT        |
| 状态恢复    | Event timeline, Dependency Graph         |
| 回放引擎    | GPU Command Stream Emulator              |
| 多 GPU 支持 | 时间戳同步，独立事件序列                 |
| 工具链      | C++17/20 + Python Binding + Rust Wrapper |

# **六、预期价值**

## **1. 技术价值**
* 为国产 / 异构 GPU 构建第一套通用 Profiling & Replay 开源工具
* 能用于 GPU 编译器、算子优化、深度学习框架调度优化
* 补齐国内 GPU 开发工具链的空白

## **2. 工程价值**
* 可作为调试 GPU 算子的重要工具
* 工程师可以复现 GPU 侧 nondeterministic bug
* 提升异构集群的 GPU 分析能力

## **3. 商业价值**
  * 可让国内 AI Infra 企业建立自己的 GPU 工具栈* 可进一步拓展至：
  * 性能优化（Performance Tuning）
  * GPU 资源调度
  * 自动化 Profiling Toolbox

# **🏁**# **七、未来扩展（2.0 / 长期规划）**
* AI 辅助 GPU 性能诊断（LLM-based Profiler Assistant）
* 与 TVM / Triton Compiler 集成
* GPU 指令级仿真器（类似 GPGPU-Sim 的轻量版本）
* 全流程自动分析报告生成
* Web 在线可视化平台

# **📌** # **八、总结（一句话定位）**
> **TraceSmith = 开源版 Nsight Systems + RenderDoc Replay 的融合体，面向国产 GPU、AI 编译器和深度学习系统的下一代 Profiling 工具。**
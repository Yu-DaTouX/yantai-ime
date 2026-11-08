# 本地整句中译英组件

本组件独立下载，不属于输入法安装包；仅在本机 CPU 上运行，无联网推理。
部署布局为用户数据目录的 models/translate-zh-en/，不得复制到输入法程序安装目录。

- 模型：Helsinki-NLP/opus-mt-zh-en（University of Helsinki NLP / OPUS-MT authors），CC-BY-4.0。
  https://huggingface.co/Helsinki-NLP/opus-mt-zh-en
  https://creativecommons.org/licenses/by/4.0/
  model.bin 是经 CTranslate2 转换的 int8 量化产物；词表及 source.spm、target.spm 来自同一模型。
  量化与格式转换改变了存储表示，非原始 FP32 权重。
- CTranslate2 4.8.1，OpenNMT，MIT。
  https://github.com/OpenNMT/CTranslate2/tree/v4.8.1
  CPU-only；WITH_MKL=OFF、WITH_DNNL=ON、WITH_CUDA=OFF，MSVC OpenMP。
- oneDNN，Apache-2.0；SentencePiece，Apache-2.0。
- SentencePiece 所需的 protobuf、Abseil、utf8-range 以及 CT2 的 cpu_features、spdlog 的
  版权与许可证随组件 licenses/ 目录保留。不得省略该目录。
- 桥接 DLL 与输入法 Server 同属本仓 GPL-3.0 软件；完整对应源码含本仓 translation-engine/ 与
  server/src/translation/。分发者须同时提供对应源码与构建命令。组件不包含 Intel MKL。

英文译文由本地统计神经翻译模型生成，可能有漏译或误译；请在重要场景确认。

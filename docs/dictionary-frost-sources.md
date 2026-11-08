# 全量白霜词库：来源、许可与本项目修改

默认中文词库为 [gaboolic/rime-frost](https://github.com/gaboolic/rime-frost)，固定提交
`af267b67162ca4098c7f8d48b77bbfc8222a10e5`，作者 gaboolic 及贡献者，GPL-3.0。
白霜原始配置和词表派生自 [iDvel/rime-ice](https://github.com/iDvel/rime-ice)
`af2480ba1b147a6a54c0c21e2997ef451c34e036`，原作者 iDvel 及贡献者，同为 GPL-3.0。
作者确认允许 GPL 客户端修改、内置、分发的原文：
https://github.com/gaboolic/rime-frost/issues/290#issuecomment-4848888810 。
根目录 LICENSE 为 GPL-3.0 全文；生成产物亦附 rime-frost-LICENSE.txt。

## 什么叫本产品的全量版

与调研中 152 MiB 那版相同的固定源：8105、41448、GB18030-2022、base、ext、others，
以及该版本默认 import 的全部 23 个 cn_dicts_cell 分包：medication、industry_product、
exthot、chess、chess2、animal、game、idiom、sport、media、shulihua、food、inputmethod、
history、place、geography、name2、literature、music、computer、composite、luna、name。
所有原始文件 URL、大小、SHA256、读取顺序都列在 scripts/dictionary_build/frost.lock.json。
原始文件地址统一为：
`https://raw.githubusercontent.com/gaboolic/rime-frost/<上述提交>/<path>`。

不包含原默认中注释掉的 tencent 大包（不在 152 MiB 对照版内）；不包含 corrections 中故意
错误的读音召回别名，避免把「角色 jiao se」当正常注音；不导入 Rime Lua、五笔、日语、英文与
数字/字母混输条目。可用词级读音原样继承，不逐字自动重注、不展开所有多音组合。

## 修改记录（2026-11）

- 使用共享 engine/contracts/dictionary/format.json 分表规则，拼音空格改单引号，ü 改 v，去调；
  校验文字与音节个数、纯汉字编码，拒收样本列在 frost-build-report.json。
- 同一 (拼音,词) 取上游最高频次，不重复累加。权重为 max(1,min(100000000,原频次×5))；
  该尺度经过同引擎离线评测，不声称是最优参数。
- 仅过滤审计确认的极低频碎片：(de'ba,的吧)、(de'le,的了)、(wo'wo,我我)、(ni'ni,你你)、
  (yu'de,鱼的)、(shi'jian'he,时间和)、(ba'a,吧啊)。每条原频次须 ≤1 才删除；上游将来若
  提高频次不会盲删。无「删所有低频词」或「删包含某字的词」的宽泛规则。只作用于出厂词库，
  用户自造词/调频/删除日志独立保留并回放，不被清洗。这些词不在固定268项覆盖探针中；
  完整覆盖、320句、延迟对照见 docs/dictionary-frost-results.md。
- 不再从旧混合中文词表导入词条；不生成已退役的 wubi86 / japanese_lexicon 表或日语模型。
  快捷短语和字母词表保留空结构，用户数据由日志恢复。

## 原始数据来源与授权边界

白霜 README 和包头说明：词频由 MNBVC 中的知乎回答等语料，经 Jieba 分词统计，再人工升降频、
多音字频次分配；README 宣称语料 745,396,750 字，本项目未重新下载语料训练。base 原来源还列有
华宇野风词表、THUOCL（MIT，Copyright 2018 THUNLP）、现代汉语常用词表、腾讯词向量，ext
参考简化字八股文；8105 注明汉字规范表与 BLCU 字频数据。cell 专业/热点包由白霜维护者整理。
保留这些出处不等于代替原权利人重新授权。仓库 GPL 声明及作者同意允许其有权授权的内容按 GPL
使用，但不能保证原始第三方词表、用户投稿及语料的全部权利链；发现具体问题应停发/移除相关
数据并联系上游。不得因此导入未取得分发许可的搜狗/QQ 商业 SCEL 文件。本构建器不下载它们。

THUOCL 的 MIT 声明：Copyright (c) 2018 THUNLP。Permission is hereby granted, free of
charge, to any person obtaining a copy of this software and associated documentation files
(the "Software"), to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so, subject to
the following conditions: The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED
"AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
来源：https://github.com/thunlp/THUOCL/blob/a30ce79d895d01ab5132a5c74c29703ff7efb4cc/LICENSE 。

## 非中文资源、源码提供与可重建

english.db 与 others.db 保持旧产品锁的非中文资源字节：MSIME-Engine dict-v1.0.0，源提交
`d0dc0c2b594b5540b5de99ad12085c786410626e`，两文件摘要列在 frost.lock.json.support。
**只下载这两个文件，不下载旧 msime.db 或日语数据**。英文/表情许可和尚未解决的商业英文词形
来源风险仍见 THIRD_PARTY_NOTICES.txt；本次换中文库不宣称消除了不相干数据的授权问题。

发行时保留本文件、根 LICENSE 与 THIRD_PARTY_NOTICES.txt；与二进制对应的完整 GPL 源包须包含
本仓构建脚本、固定锁及上述提交的原始白霜源文件（可由 python scripts/build-dictionary.py
下载到 MetasequoiaImeDict/cache/frost/<commit>/，按锁复核）。仅提供会变化的分支名不够。
需要离线重新构建时归档该缓存，不使用浮动 HEAD。规则和产物的规范记录摘要都纳入产品锁；
SQLite 自身版本可能改变页布局，故跨平台复建比较规范词条摘要，而非冒称 SQLite 字节永远相同。

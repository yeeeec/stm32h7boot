# STM32H7 Bootloader 文件系统升级触发契约

> 版本：v3。文件名为兼容旧链接保留；本契约不再定义“可信请求”。

本文冻结文件布局、请求触发语义、完整性检查、陈旧包识别、提交/清理顺序和 Package Source 接口边界。

## 1. 安全与完整性模型

请求文件不是信任边界：

- `/boot_update_request.json` 的存在只表示“尝试处理固定目录中的发布包”；
- Bootloader 不打开、不解析、不校验请求文件内容；
- 请求不绑定 `package_id`、`manifest_sha256`、nonce、状态或签名；
- FAT 文件可以被修改，因此请求文件不提供来源认证、发布授权或防攻击者替换；
- Bootloader 不执行 Manifest ECDSA 验签，不持有公钥，不选择 Key ID；
- Manifest、APP 和 GUI 的 SHA-256 以及目标 CRC 只用于检测格式、传输和存储损坏，不等价于来源认证。

若产品需要密码学发布认证，必须在本契约之外增加明确的可信启动或发布认证机制；不得把普通请求文件描述为认证凭据。

## 2. 固定文件布局

```text
/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin
    └── hmi.gui.bin
```

规则：

- 路径大小写固定，不扫描目录、不接受别名；
- 发布工具先写入并关闭 `firmware/` 下三个文件，最后创建请求文件；
- 请求文件允许为空或包含任意内容，因为 Bootloader 不读取内容；
- 同一时刻只支持一个发布包；
- 请求存在但任一发布文件缺失、不可读或格式错误时，不擦除目标槽，不删除请求，回到当前激活对启动路径。

## 3. Application 读取顺序

```text
查询介质
-> 挂载
-> exists("/boot_update_request.json")
-> UpdateService_PrepareStart()
-> 等待 Prepare 终态
-> Application 执行陈旧与版本决策
-> UpdateService_InstallStart(active_record)
-> 等待 Install 终态并取得候选
-> BootControlService_CommitActiveStart(candidate)
-> 等待提交终态
-> remove("/boot_update_request.json")（尽力执行）
-> 卸载
-> system_reset.request()
```

Package Source 的 `mount/unmount/exists/remove` 由 Application 调用。Update Service 只在介质已经挂载时使用 `open/get_size/read_at/close` 读取固定发布文件。

## 4. Manifest 与组件完整性

Update Service 的 Prepare 阶段：

1. 打开固定 `manifest.json`；
2. 限制文件大小并读取完整字节；
3. 对完整原始文件计算 SHA-256；
4. 严格解析冻结 Schema；
5. 返回解析后的 `validated_manifest_t`。

Manifest Service 不执行：

- ECDSA 或其他签名验证；
- 公钥、Key ID 或轮换选择；
- 签名输入 Canonicalization；
- 请求文件认证或绑定。

Install 擦除前必须完成：

- Manifest 已成功 Prepare；
- 产品、硬件和格式字段有效；
- Application 已接受最低 Bootloader 版本和升级版本；
- APP/GUI 文件大小与 Manifest 一致；
- APP 完整文件 SHA-256 与 Manifest 一致；
- APPX Header、重定位表和目标地址范围有效；
- GUI 完整文件 SHA-256 与 Manifest 一致。

目标写入后必须分别计算目标 APP/GUI CRC32，并与候选 Active Record 的期望值一致。

## 5. 陈旧请求与发布包身份

Active Record 保存：

- `package_id_hash128`：Manifest `package_id` 的 UTF-8 SHA-256 前 16 字节；
- `manifest_sha256`：本次实际读取的完整 Manifest 文件 SHA-256。

Application 仅在两个字段都相同时判定发布包已经激活：

```text
same_package =
    active.package_id_hash128 == manifest.package_id_hash128
    && active.manifest_sha256 == manifest.manifest_sha256
```

相同发布包的陈旧请求：

- 不调用 Install；
- 不擦除或编程任何槽；
- 尝试删除请求并卸载；
- 删除失败也直接验证并启动当前激活对，不形成“相同包重复擦写”或复位循环。

只有 `package_id` 相同而 Manifest SHA 不同，不视为相同发布包；仍需继续执行版本规则。版本不高于当前激活版本时拒绝 Install，因此不会擦写。

## 6. 提交、删除与复位

新安装的强制顺序：

```text
目标 APP/GUI 校验成功
-> Update Service 返回未提交候选
-> Application 原子提交 Active Record
-> Application 尝试删除请求
-> Application 卸载介质
-> Application 请求系统复位
```

不变量：

- Active Record 提交前不得删除请求；
- Update Service 不得提交 Active Record；
- 请求删除失败不得回退或破坏新 Active Record；
- 请求删除失败不改变“升级成功”结论；
- 新 Active Record 提交成功后统一进入系统复位，不直接跳转新镜像；
- 安装或提交失败时不复位到未提交的新槽，而是验证/启动原激活对。

请求删除是清理操作，不是事务提交点。Active Record 的持久化提交是唯一激活提交点。

## 7. 掉电矩阵

| 掉电点 | EEPROM Active Record | 请求 | 下次启动 |
|---|---|---|---|
| 创建请求前 | 旧 | 无 | 启动旧对 |
| 请求已创建，Prepare 前/中 | 旧 | 有 | 重新 Prepare，不擦写或按规则继续 |
| 非激活槽擦写中 | 旧 | 有 | 重新安装非激活槽，旧对仍可启动 |
| 候选生成后、提交前 | 旧 | 有 | 重新安装或校验，旧对仍激活 |
| Active Record 提交中 | 旧或新 | 有 | A/B 选择有效最新记录 |
| 提交后、删除前 | 新 | 有 | 识别相同发布包，只清理、不重写 |
| 删除失败后复位 | 新 | 可能有 | 识别陈旧请求，直接启动新对 |
| 删除后、复位前 | 新 | 无 | 启动新对 |

## 8. `package_source_t` 合同

接口提供：

- `is_media_present(context, &present)`：查询可移除介质；
- `mount(context)` / `unmount(context)`：挂载与卸载单一卷；
- `exists(context, path, &present)`：不打开文件地探测固定路径；
- `remove(context, path)`：在已挂载且无打开文件时删除请求；
- `open(context, path)` / `close(context)`：管理唯一打开文件；
- `get_size(context, &size)`：读取当前文件大小；
- `read_at(context, offset, data, size, &bytes_read)`：绝对偏移读取。

边界：

- Application 使用介质、挂载、请求探测/删除操作；
- Update Service 使用只读文件操作；
- Interface 不表达请求可信性、认证、版本策略或提交语义；
- FatFs 与未来 eMMC 的差异只存在于 Adapter。

## 9. 验收条件

- [ ] EEPROM 地址空间只定义 Active Record A/B；
- [ ] Bootloader ELF 不含 Manifest 验签、公钥、Micro-ECC/uECC 符号；
- [ ] 请求文件内容改变不影响触发语义；
- [ ] 没有请求时不进入 Prepare/Install；
- [ ] 相同 `package_id_hash128 + manifest_sha256` 不擦写；
- [ ] Update/Recovery Service 不挂载、不删除请求、不提交 EEPROM、不复位；
- [ ] Active Record 提交成功后才尝试删除请求；
- [ ] 删除失败保留新激活对并复位；
- [ ] 新安装成功统一复位；
- [ ] 掉电测试满足第 7 节矩阵。

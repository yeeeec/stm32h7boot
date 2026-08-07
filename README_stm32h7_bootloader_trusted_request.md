# STM32H7 Bootloader 文件系统升级文档集

本文件是当前实现的入口。文件名为兼容既有链接保留；“trusted_request”不再表示请求文件具备认证能力。

## 固定目录

```text
/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin
    ├── hmi.app.reloc.bin
    └── hmi.gui.bin
```

`/boot_update_request.json` 只按固定路径的存在性触发升级。Bootloader 不读取或解释其内容，也不把它作为发布包的可信凭据。

## 当前升级链

```text
Application 探测请求文件
-> Update Service 读取并严格解析 Manifest、计算 Manifest SHA-256
-> Application 执行陈旧包、最低 Bootloader 版本和升级版本决策
-> Update Service 校验源文件并安装到非激活槽，返回 Active Record 候选
-> Application 调用 Boot Control Service 原子提交 Active Record
-> Application 尝试删除请求文件并卸载介质
-> Application 统一请求系统复位
```

固定规则：

- EEPROM 只保存 Active Record A/B，不保存 Update Request；
- Bootloader 不执行 Manifest ECDSA 验签，不装配公钥或签名验证器；
- 请求文件负责触发，不负责认证或 Manifest 内容绑定；
- Manifest 与 APP/GUI 的 SHA-256 用于完整性检查，不等价于来源认证；
- `package_id_hash128 + manifest_sha256` 与当前 Active Record 相同时，请求属于陈旧请求，只尝试清理，不擦写 Flash；
- 请求文件只能在新 Active Record 提交成功后删除；陈旧请求也可直接清理；
- 新 Active Record 提交后，请求删除失败不回退激活对，仍进入系统复位；
- 安装或提交失败时保留原激活对，并回到原激活对验证/启动路径；
- Recovery Service 只验证并返回恢复候选，Application 决定是否提交和启动。

## 文档阅读顺序

1. `stm32h7_bootloader_filesystem_trusted_request_contract_v2.md`：文件布局、请求触发、完整性与掉电顺序。
2. `stm32h7_bootloader_business_rules_v0.8_trusted_request.md`：顶层业务规则、模式选择、升级与恢复。
3. `stm32h7_bootloader_detailed_design_v0.3_trusted_request.md`：记录格式、状态机、Service/Interface/Adapter 和测试。
4. `stm32h7_application_services_codex_architecture_trusted_request.md`：Application/Services 职责边界。
5. `stm32h7_firmware_software_architecture_trusted_request.md`：通用分层、Composition 和构建依赖。

## 禁止重新引入的设计

- 不得把 Update Request 保存到 EEPROM；
- 不得把请求文件内容当作可信认证；
- 不得在 Bootloader 中加入 Manifest 验签、公钥或密钥选择逻辑；
- 不得让 Update/Recovery Service 探测或删除请求、提交 Active Record、决定版本策略或执行系统复位；
- 不得在 Active Record 提交前删除请求；
- 不得因请求删除失败回退已经提交的新激活对；
- 不得让相同发布包的陈旧请求重复擦写非激活槽。

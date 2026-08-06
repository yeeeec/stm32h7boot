# STM32H7 Bootloader SD/eMMC 文件系统升级文档集

> 修订主题：可信升级请求承担发布授权；Bootloader 不重复验签，只执行 SHA 完整性和安装校验。

## 固定目录

```text
/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    ├── hmi.app.bin
    └── hmi.gui.bin
```

## 核心信任链

```text
Application 验证 Manifest ECDSA 签名
-> 创建包含 manifest_sha256 的可信请求
-> Bootloader 校验 Manifest SHA-256
-> Bootloader 校验 APP/GUI SHA-256
-> 安装非激活槽并提交 Active Record
```

关键结论：

- 请求文件 V2 同时承担触发和可信授权；
- Bootloader 不持有 Manifest 公钥，不链接 Manifest 验签实现；
- 请求通过 `manifest_sha256` 绑定一个确定的 Manifest；
- APP/GUI Hash 从已绑定 Manifest 取得；
- Bootloader 仍执行格式、兼容性、版本、防回滚、APPX、重定位、目标 CRC 和事务提交；
- 当前人工 SD 方式属于开发 trust override；普通 FAT 文件本身不具备量产级防篡改能力；
- 正式 eMMC/文件系统实现必须保证请求只能由受信 Application 或生产工具创建。

## Codex 阅读顺序

1. `stm32h7_bootloader_filesystem_trusted_request_contract_v2.md`  
   先冻结请求 V2、信任交接、SHA 顺序和 Request Store 安全合同。

2. `stm32h7_bootloader_business_rules_v0.8_trusted_request.md`  
   冻结顶层业务、不变量、错误决策、恢复和提交顺序。

3. `stm32h7_bootloader_detailed_design_v0.3_trusted_request.md`  
   实现级状态机、Service/Interface/Adapter、内存、错误和测试。

4. `stm32h7_application_services_codex_architecture_trusted_request.md`  
   约束 Application/Services 边界和 Composition 绑定。

5. `stm32h7_firmware_software_architecture_trusted_request.md`  
   约束通用层次、Generated Integration、Adapter 和构建依赖。

## 不得恢复的旧规则

- 不得把请求降级为普通触发文件；
- 不得在 Bootloader 中再次加入 Manifest ECDSA 验签；
- 不得只比较 `package_id` 而不比较 `manifest_sha256`；
- 不得在 Manifest SHA 绑定前信任 Manifest 内容；
- 不得把当前可人工修改的 SD 请求误称为量产级密码学认证；
- 不得在 Active Record 提交前删除请求。

====================================================
# USB CCK 硬件指纹协议说明 (Binary Version)
====================================================

1. 采集维度 (Components):
   - VID: 厂商识别码 (4位Hex)
   - PID: 产品识别码 (4位Hex)
   - SN : 硬件序列号 (Physical Serial Number)
   - VOL: 逻辑卷序列号 (Volume Serial Number)
   - SALT: 自定义扰动值 (内置私钥字符串)

2. 生成算法 (Algorithm):
   - 拼接: RAW = VID + PID + SN + VOL + SALT
   - 计算: Fingerprint = CRC32_ISO(UTF8(RAW))
   - 转换: Result = 32-bit Unsigned Integer (Raw Binary)

3. 输出规范 (Storage):
   - 文件名: cck (无后缀，全小写)
   - 位置: U盘根目录 (\cck)
   - 属性: 只读 (ReadOnly) + 隐藏 (Hidden)
   - 内容: 4 字节原始二进制数据 (Little Endian)
   - 文件大小: 精确为 4 Bytes

4. 校验逻辑 (Verification):
   - 实时采样U盘参数 -> 执行算法 -> 读取 cck 内 4 字节数据进行位比对
   - 匹配则授权通过；内容不符、大小异常或文件缺失则判定为非法复制
====================================================
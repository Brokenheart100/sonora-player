// 二维码矩阵 —— 只算"哪一格是黑的"，不碰任何图片格式。
//
// 为什么不让服务端给图：明文接口 `/api/login/qrcode/create` 返回 404，
// 只有 `/api/login/qrcode/unikey` 给 unikey，二维码得自己画（见 docs/ONLINE.md §16.5）。
// 也不生成 PNG：界面逐格画更省事，还不用引图片插件、不用管 base64。
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sonora::online::core::auth
{

    // 返回 (2*size+1) 行 × (2*size+1) 列的 "0"/"1" 字符串：1 = 黑格。
    // 外面留一圈白边（quiet zone），是扫码器认得出的必要部分。
    // 编码失败（文本为空 / 超长）返回空 vector。
    std::vector<std::string> encodeQrRows(std::string_view text);

} // namespace sonora::online::core::auth

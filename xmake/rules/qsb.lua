-- xmake qsb —— 把 assets/shaders/*.frag 编成 .qsb 放进 qrc。
--
-- 为什么需要自己写：xmake 的 qt 规则集里**没有 qsb**（只有 moc/qrc/ts/ui/
-- qmltyperegistrar，见 ARCHITECTURE.md §9.5）。Qt 的 qt_add_shaders() 在这里没有
-- 等价物，所以手写一个 task。
--
-- 用法（先把 Qt 的 bin 放进 PATH，或者设 QTDIR）：
--   PATH="/d/Software/Qt/6.12.0/msvc2022_64/bin:$PATH" xmake qsb
--   QTDIR=/d/Software/Qt/6.12.0/msvc2022_64 xmake qsb
--
-- 产物提交进仓库（.qsb 是二进制但很小），这样不装 Qt SDK 的人也能构建。
-- 改了 .frag 就一定要重跑一次，否则跑的是旧 shader。
--
-- ── 两个踩过的坑，别再踩 ────────────────────────────────────────────────
-- 1. 这个文件是被 includes() 进来的 task 描述，**只允许 description 级 API**：
--    import() 在这里报 `unknown interface: import()`，pcall() 报
--    `unknown interface: pcall()`。所以不要指望在文件里 find_qt。
-- 2. on_run 的沙箱里同样没有 import。找工具只能用 os.getenv / PATH。

local FRAG_DIR = path.join(os.projectdir(), "assets", "shaders")

-- 找 qsb：先看 QTDIR，再退回 PATH 里的 qsb。
local function find_qsb()
    local qtdir = os.getenv("QTDIR")
    if qtdir then
        local candidate = path.join(qtdir, "bin", is_host("windows") and "qsb.exe" or "qsb")
        if os.isfile(candidate) then
            return candidate
        end
    end
    return "qsb"
end

task("qsb")
    set_category("plugin")
    set_menu({
        usage = "xmake qsb",
        description = "把 assets/shaders/*.frag 编成 .qsb（Qt 自定义 shader）"
    })
    on_run(function ()
        local qsb = find_qsb()
        local frags = os.files(path.join(FRAG_DIR, "*.frag"))
        if #frags == 0 then
            print("没有找到 %s 下的 .frag", FRAG_DIR)
            return
        end
        for _, frag in ipairs(frags) do
            local output = frag .. ".qsb"
            print("qsb %s -> %s", path.filename(frag), path.filename(output))
            os.vrunv(qsb, {"--qt6", frag, "-o", output})
        end
        print("完成：%d 个 shader", #frags)
    end)

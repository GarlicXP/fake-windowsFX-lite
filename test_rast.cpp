// 自动生成的 RasterizeRows 单元测试：验证降分辨率一致性、4x 展开、行分段
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <random>

struct DragCtx {
    int  w = 0, h = 0;
    double x = 0, y = 0;
    double skewX = 0, skewY = 0;
    double stretchX = 1, stretchY = 1;
    double introScaleX = 1, introScaleY = 1;
    void* snapBits = nullptr;
    int snapW = 0;
    int visOffX = 0, visOffY = 0;
    bool snapOpaque = true;
};

static void RasterizeRows(DragCtx& d, int minx, int miny, int pw, int ph,
                          double scale, uint32_t* out, int allocW,
                          int y0, int y1)
{
    const double A = d.stretchX * d.introScaleX;
    const double B = d.stretchY * d.introScaleY;
    const double kPi = 3.14159265358979323846;
    const double tx = std::tan(d.skewX * kPi / 180.0);
    const double ty = std::tan(d.skewY * kPi / 180.0);
    double K = 1.0 - tx * ty;
    if (std::abs(K) < 1e-9) K = (K < 0 ? -1e-9 : 1e-9);
    const double cx = d.w * 0.5, cy = d.h * 0.5;
    const double invS = 1.0 / scale;
    // 16.16 定点步长：全分辨率下 fdu = 65536/(A*K)；scale 分辨率下每个
    // 目标像素对应全分辨率 1/scale 像素，步长相应放大（恒 > 0）
    const int fdu = (int)std::llround(65536.0 / (A * K) * invS);
    const int fdv = (int)std::llround(-65536.0 * ty / (B * K) * invS);
    const int fcx = (int)std::llround(cx * 65536.0);
    const int fcy = (int)std::llround(cy * 65536.0);
    const double dx0 = (double)minx - d.x - cx;
    const uint32_t* src = (const uint32_t*)d.snapBits;
    const int sw = d.snapW;
    const int w = d.w, h = d.h;
    // 窗口内像素一律不透明（黑色内容也是）：边界已由 px/py 范围检查
    // 明确判定，不再用旧 PlgBlt 管线"rgb==0 即透明"的 hack——那个
    // hack 会把纯黑文字/背景误判成窗口外而变透明。
    // snapOpaque（快照 alpha 已强制 255）时直接补 alpha；否则保留源
    // alpha（PrintWindow 通常返回 255，真实透明窗口则保持其透明度）。
    const bool opaque = d.snapOpaque;
    const long long wLimit = ((long long)w << 16) - 0x8000LL;

    for (int Y = y0; Y < y1; ++Y) {
        const double dy = (double)(miny + (double)Y * invS) - d.y - cy;
        int fu = (int)std::llround(((dx0 - tx * dy) / (A * K)) * 65536.0) + fcx;
        int fv = (int)std::llround(((dy - ty * dx0) / (B * K)) * 65536.0) + fcy;
        uint32_t* o = out + (size_t)Y * allocW;

        // 行级快速路径：整行 py 不变（垂直倾斜为零或极小，水平拖动的
        // 常态）时，py 只判定一次，px 用解析跨度切成三段，中段拷贝
        // 无逐像素边界分支——慢机器上这是主要的每帧 CPU 开销
        const int pyA = (fv + 0x8000) >> 16;
        const int pyB = (int)((((long long)fv + (long long)(pw - 1) * fdv) + 0x8000) >> 16);
        if (pyA == pyB) {
            if ((unsigned)pyA >= (unsigned)h) {
                memset(o, 0, (size_t)pw * 4);          // 整行在窗口外
                continue;
            }
            const uint32_t* srcRow = src + (size_t)(pyA + d.visOffY) * sw + d.visOffX;
            // 解析 px∈[0,w) 对应的 X 跨度 [Xs, Xe)
            int Xs = 0, Xe = pw;
            long long lo = -0x8000LL - fu;             // px>=0 ⟺ X*fdu >= lo
            if (lo > 0) {
                Xs = (int)((lo + fdu - 1) / fdu);      // ceil
                if (Xs > pw) Xs = pw;
            }
            long long hi = wLimit - fu;                // px<w ⟺ X*fdu < hi
            if (hi <= 0) {
                Xe = 0;
            } else {
                Xe = (int)((hi + fdu - 1) / fdu);      // ceil = 首个越界 X
                if (Xe > pw) Xe = pw;
            }
            if (Xs > Xe) Xs = Xe;
            if (Xs > 0) memset(o, 0, (size_t)Xs * 4);
            // 累加用 64 位：scale<1 时 fdu 可达 ~50 万，4K 行宽下 int 会溢出
            long long f = (long long)fu + (long long)Xs * fdu;
            const long long fdu4 = (long long)fdu * 4;
            if (opaque) {
                // 4 像素展开：4 次采样互不依赖，编译器可并行发射
                int X = Xs;
                const int Xe4 = Xs + ((Xe - Xs) & ~3);
                for (; X < Xe4; X += 4) {
                    o[X]     = srcRow[(int)((f            + 0x8000LL) >> 16)] | 0xFF000000u;
                    o[X + 1] = srcRow[(int)((f +   fdu    + 0x8000LL) >> 16)] | 0xFF000000u;
                    o[X + 2] = srcRow[(int)((f + 2 * fdu  + 0x8000LL) >> 16)] | 0xFF000000u;
                    o[X + 3] = srcRow[(int)((f + 3 * fdu  + 0x8000LL) >> 16)] | 0xFF000000u;
                    f += fdu4;
                }
                for (; X < Xe; ++X) {
                    o[X] = srcRow[(int)((f + 0x8000LL) >> 16)] | 0xFF000000u;
                    f += fdu;
                }
            } else {
                int X = Xs;
                const int Xe4 = Xs + ((Xe - Xs) & ~3);
                for (; X < Xe4; X += 4) {
                    o[X]     = srcRow[(int)((f            + 0x8000LL) >> 16)];
                    o[X + 1] = srcRow[(int)((f +   fdu    + 0x8000LL) >> 16)];
                    o[X + 2] = srcRow[(int)((f + 2 * fdu  + 0x8000LL) >> 16)];
                    o[X + 3] = srcRow[(int)((f + 3 * fdu  + 0x8000LL) >> 16)];
                    f += fdu4;
                }
                for (; X < Xe; ++X) {
                    o[X] = srcRow[(int)((f + 0x8000LL) >> 16)];
                    f += fdu;
                }
            }
            if (Xe < pw) memset(o + Xe, 0, (size_t)(pw - Xe) * 4);
            continue;
        }

        // 通用路径：py 逐像素变化（垂直倾斜较大，多为瞬态），64 位累加防溢出
        long long fuL = fu, fvL = fv;
        for (int X = 0; X < pw; ++X) {
            const int px = (int)((fuL + 0x8000LL) >> 16);
            const int py = (int)((fvL + 0x8000LL) >> 16);
            if ((unsigned)px < (unsigned)w && (unsigned)py < (unsigned)h) {
                const uint32_t p = src[(size_t)(py + d.visOffY) * sw + (px + d.visOffX)];
                o[X] = opaque ? (p | 0xFF000000u) : p;
            } else {
                o[X] = 0;
            }
            fuL += fdu; fvL += fdv;
        }
    }
}

static void MakeSnapshot(DragCtx& d, int w, int h, unsigned seed)
{
    std::mt19937 rng(seed);
    d.w = w; d.h = h; d.snapW = w;
    std::vector<uint32_t> buf((size_t)w * h);
    for (auto& v : buf) v = (uint32_t)rng() | 0xFF000000u;   // 不透明随机像素
    d.snapBits = buf.data();
}

// 朴素逐像素参考：复刻 RasterizeRows 的整数定点管线（无展开、逐像素），
// 用于校验 4x 展开与跨度切分没有引入错误
static uint32_t RefSample(const DragCtx& d, int minx, int miny, int ox, int oy)
{
    const double A = d.stretchX * d.introScaleX;
    const double B = d.stretchY * d.introScaleY;
    const double kPi = 3.14159265358979323846;
    const double tx = std::tan(d.skewX * kPi / 180.0);
    const double ty = std::tan(d.skewY * kPi / 180.0);
    double K = 1.0 - tx * ty;
    if (std::abs(K) < 1e-9) K = (K < 0 ? -1e-9 : 1e-9);
    const double cx = d.w * 0.5, cy = d.h * 0.5;
    const double dx0 = (double)minx - d.x - cx;
    const double dy = (double)(miny + oy) - d.y - cy;
    const int fdu = (int)std::llround(65536.0 / (A * K));
    const int fdv = (int)std::llround(-65536.0 * ty / (B * K));
    int fu = (int)std::llround(((dx0 - tx * dy) / (A * K)) * 65536.0) + (int)std::llround(cx * 65536.0);
    int fv = (int)std::llround(((dy - ty * dx0) / (B * K)) * 65536.0) + (int)std::llround(cy * 65536.0);
    long long f = (long long)fu + (long long)ox * fdu;
    long long g = (long long)fv + (long long)ox * fdv;
    int px = (int)((f + 0x8000LL) >> 16);
    int py = (int)((g + 0x8000LL) >> 16);
    if (px < 0 || px >= d.w || py < 0 || py >= d.h) return 0;
    const uint32_t* src = (const uint32_t*)d.snapBits;
    uint32_t p = src[(size_t)(py + d.visOffY) * d.snapW + (px + d.visOffX)];
    return d.snapOpaque ? (p | 0xFF000000u) : p;
}

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); ++g_fail; } } while (0)

static void CheckAgainstRef(const DragCtx& d, int minx, int miny, int bw, int bh,
                            const std::vector<uint32_t>& buf, int stride, const char* tag)
{
    for (int j = 0; j < bh; ++j)
        for (int i = 0; i < bw; ++i) {
            uint32_t got = buf[(size_t)j * stride + i];
            uint32_t ref = RefSample(d, minx, miny, i, j);
            CHECK(got == ref, tag);
        }
}

int main()
{
    // ---------- 场景 1：恒等变换（skew=0, stretch=1）----------
    {
        DragCtx d; MakeSnapshot(d, 256, 192, 1234);
        d.x = 100.0; d.y = 80.0;
        int minx = 90, miny = 70;
        int bw = 200, bh = 160;
        std::vector<uint32_t> A((size_t)256 * 192, 0xDEADBEEFu), B((size_t)128 * 96, 0xDEADBEEFu);
        RasterizeRows(d, minx, miny, bw, bh, 1.0, A.data(), 256, 0, bh);
        RasterizeRows(d, minx, miny, 100, 80, 0.5, B.data(), 128, 0, 80);
        // 全分辨率 vs 朴素参考（校验 4x 展开 + 跨度切分）
        CheckAgainstRef(d, minx, miny, bw, bh, A, 256, "identity: full == naive ref");
        // 降分辨率 = 全分辨率隔点采样（恒等变换下应逐像素相等）
        for (int j = 0; j < 80; ++j)
            for (int i = 0; i < 100; ++i)
                CHECK(B[(size_t)j * 128 + i] == A[(size_t)(2 * j) * 256 + 2 * i],
                      "identity: scaled == full at aligned pixel");
    }

    // ---------- 场景 2：倾斜+拉伸（含垂直倾斜 → 通用路径）----------
    {
        DragCtx d; MakeSnapshot(d, 320, 240, 777);
        d.x = 40.0; d.y = 30.0;
        d.skewX = 8.0; d.skewY = 4.0;
        d.stretchX = 1.12; d.stretchY = 0.94;
        int minx = 30, miny = 20;
        int bw = 280, bh = 220;
        std::vector<uint32_t> A((size_t)320 * 256, 0), B((size_t)160 * 128, 0);
        RasterizeRows(d, minx, miny, bw, bh, 1.0, A.data(), 320, 0, bh);
        RasterizeRows(d, minx, miny, 140, 110, 0.5, B.data(), 160, 0, 110);
        CheckAgainstRef(d, minx, miny, bw, bh, A, 320, "skew: full == naive ref");
        // 降分辨率与全分辨率在 ±1px 邻域内一致（定点步长取整亚像素漂移）
        for (int j = 0; j < 110; ++j)
            for (int i = 0; i < 140; ++i) {
                uint32_t b = B[(size_t)j * 160 + i];
                bool found = false;
                for (int dj = -1; dj <= 1 && !found; ++dj)
                    for (int di = -1; di <= 1 && !found; ++di) {
                        int cx = 2 * i + di, cy = 2 * j + dj;
                        if (cx < 0 || cx >= bw || cy < 0 || cy >= bh) continue;
                        if (A[(size_t)cy * 320 + cx] == b) found = true;
                    }
                if (!found) {
                    // 边界列/行允许邻域放宽到 ±2（亚像素漂移跨过取整边界的像素）
                    for (int dj = -2; dj <= 2 && !found; ++dj)
                        for (int di = -2; di <= 2 && !found; ++di) {
                            int cx = 2 * i + di, cy = 2 * j + dj;
                            if (cx < 0 || cx >= bw || cy < 0 || cy >= bh) continue;
                            if (A[(size_t)cy * 320 + cx] == b) found = true;
                        }
                }
                CHECK(found, "skew: scaled sample within +-2px of full");
            }
    }

    // ---------- 场景 3：行分段（y0/y1）与整段一致 + 窄行宽尾循环 ----------
    {
        DragCtx d; MakeSnapshot(d, 200, 150, 42);
        d.x = 5.0; d.y = 6.0;
        d.skewX = -6.0; d.skewY = 2.0;
        d.stretchX = 0.98; d.stretchY = 1.05;
        int minx = 2, miny = 3, bw = 190, bh = 140;
        std::vector<uint32_t> A((size_t)200 * 160, 0), B((size_t)200 * 160, 0);
        RasterizeRows(d, minx, miny, bw, bh, 1.0, A.data(), 200, 0, bh);
        RasterizeRows(d, minx, miny, bw, bh, 1.0, B.data(), 200, 0, 35);
        RasterizeRows(d, minx, miny, bw, bh, 1.0, B.data(), 200, 35, 90);
        RasterizeRows(d, minx, miny, bw, bh, 1.0, B.data(), 200, 90, bh);
        for (int j = 0; j < bh; ++j)
            for (int i = 0; i < bw; ++i)
                CHECK(A[(size_t)j * 200 + i] == B[(size_t)j * 200 + i],
                      "row banding identical to full pass");
        // 窄行宽（pw=7）测 4x 展开的尾循环
        std::vector<uint32_t> C((size_t)200 * 160, 0);
        RasterizeRows(d, minx, miny, 7, 7, 1.0, C.data(), 200, 0, 7);
        for (int j = 0; j < 7; ++j)
            for (int i = 0; i < 7; ++i)
                CHECK(C[(size_t)j * 200 + i] == A[(size_t)j * 200 + i],
                      "narrow tail loop identical to full pass");
    }

    // ---------- 场景 4：scale=0.25 降到底 ----------
    {
        DragCtx d; MakeSnapshot(d, 400, 300, 999);
        d.x = 20.0; d.y = 25.0;
        d.skewX = -3.0; d.skewY = 5.0;
        d.stretchX = 1.06; d.stretchY = 0.97;
        int minx = 10, miny = 15, bw = 380, bh = 270;
        std::vector<uint32_t> A((size_t)400 * 288, 0), B((size_t)100 * 72, 0);
        RasterizeRows(d, minx, miny, bw, bh, 1.0, A.data(), 400, 0, bh);
        RasterizeRows(d, minx, miny, 95, 68, 0.25, B.data(), 100, 0, 68);
        for (int j = 0; j < 68; ++j)
            for (int i = 0; i < 95; ++i) {
                uint32_t b = B[(size_t)j * 100 + i];
                bool found = false;
                for (int dj = -2; dj <= 2 && !found; ++dj)
                    for (int di = -2; di <= 2 && !found; ++di) {
                        int cx = 4 * i + di, cy = 4 * j + dj;
                        if (cx < 0 || cx >= bw || cy < 0 || cy >= bh) continue;
                        if (A[(size_t)cy * 400 + cx] == b) found = true;
                    }
                CHECK(found, "scale0.25: sample within +-2px of full");
            }
    }

    // ---------- 场景 5：快照含真实 alpha（不透明窗口置 255 语义）----------
    {
        DragCtx d; MakeSnapshot(d, 128, 96, 555);
        d.snapOpaque = false;
        std::vector<uint32_t> buf((size_t)128 * 96);
        std::mt19937 rng2(556);
        for (auto& v : buf)
            v = ((uint32_t)rng2() & 0x00FFFFFFu) | (((uint32_t)rng2() & 0x3Fu) << 24);
        std::memcpy(d.snapBits, buf.data(), buf.size() * 4);
        d.x = 8.0; d.y = 9.0; d.skewX = 3.0; d.stretchX = 1.02;
        int minx = 5, miny = 6, bw = 110, bh = 80;
        std::vector<uint32_t> A((size_t)128 * 96, 0);
        RasterizeRows(d, minx, miny, bw, bh, 1.0, A.data(), 128, 0, bh);
        CheckAgainstRef(d, minx, miny, bw, bh, A, 128, "alpha: full == naive ref");
    }

    if (g_fail == 0) { printf("ALL RASTER TESTS PASSED\n"); return 0; }
    printf("%d FAILURES\n", g_fail);
    return 1;
}

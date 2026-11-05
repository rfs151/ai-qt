#include "comm/TelemetryStore.h"

#include <QtMath>
#include <climits>

void TelemetryStore::append(const Sample &s)
{
    auto &ptr = m_series[s.deviceId];
    if (!ptr)
        ptr = std::make_shared<TelemetrySeries>(m_capacity);
    ptr->push({s.tMs, s.v});
    ++m_version;
}

const TelemetrySeries *TelemetryStore::series(int deviceId) const
{
    auto it = m_series.constFind(deviceId);
    return it == m_series.cend() ? nullptr : it->get();
}

void TelemetryStore::remove(int deviceId)
{
    m_series.remove(deviceId);
    ++m_version;
}

void TelemetryStore::clear()
{
    m_series.clear();
    ++m_version;
}

void TelemetryStore::setSeries(int deviceId, const QVector<TelemetryPoint> &points)
{
    auto s = std::make_shared<TelemetrySeries>(std::max<qsizetype>(1, points.size()));
    for (const TelemetryPoint &p : points)
        s->push(p);
    m_series[deviceId] = std::move(s);
    ++m_version;
}

void TelemetryStore::fillSynthetic(int deviceId, qsizetype count, qint64 endMs, int stepMs)
{
    auto s = std::make_shared<TelemetrySeries>(count);
    const qint64 start = endMs - (count - 1) * stepMs;
    double walk = 0;
    quint32 x = 12345;
    for (qsizetype i = 0; i < count; ++i) {
        const double t = i * stepMs / 1000.0;
        x = x * 1664525u + 1013904223u;   // 简单 LCG，比 QRandomGenerator 快，压测数据不需要高质量随机数
        const double r = (x >> 8) / double(1 << 24) - 0.5;
        walk = std::clamp(walk + r * 0.8, -15.0, 15.0);
        TelemetryPoint p;
        p.t = start + i * stepMs;
        // 叠加不同频率的正弦 + 噪声 + 偶发尖峰，降采样后应该仍能看到尖峰
        const double spike = (i % 9973 == 0) ? 40 : 0;
        p.v = {float(50 + 20 * std::sin(t * 0.7) + 8 * std::sin(t * 11.0) + r * 6 + spike),
               float(55 + walk),
               float(500 + 300 * std::sin(t * 0.13) + r * 80),
               float(260 + 150 * std::sin(t * 0.13 + 1) + r * 40)};
        s->push(p);
    }
    m_series[deviceId] = std::move(s);
    ++m_version;
}

DecimateResult decimateMinMax(const TelemetrySeries &s, int channel, qint64 t0, qint64 t1, int width, qint64 gapMs)
{
    DecimateResult r;
    if (s.isEmpty() || width <= 0 || t1 <= t0)
        return r;

    auto key = [](const TelemetryPoint &p) { return p.t; };
    qsizetype i0 = s.lowerBound(t0, key);
    qsizetype i1 = s.lowerBound(t1, key);
    // 两端各多取一个点，让折线从窗口外延伸进来，而不是在边缘断掉
    if (i0 > 0)
        --i0;
    if (i1 < s.size())
        ++i1;
    r.inputPoints = i1 - i0;
    if (r.inputPoints <= 0)
        return r;

    const double scale = double(width) / double(t1 - t0);
    auto xOf = [&](qint64 t) { return double(t - t0) * scale; };

    r.minY = std::numeric_limits<double>::max();
    r.maxY = std::numeric_limits<double>::lowest();
    QVector<QPointF> *seg = nullptr;
    auto newSegment = [&] {
        r.segments.append(QVector<QPointF>());
        seg = &r.segments.last();
    };
    auto put = [&](qsizetype i) {
        const TelemetryPoint &p = s.at(i);
        const double y = p.v[size_t(channel)];
        seg->append(QPointF(xOf(p.t), y));
        r.minY = std::min(r.minY, y);
        r.maxY = std::max(r.maxY, y);
        ++r.outputPoints;
    };

    // 点数不多时不抽取，原样输出
    if (r.inputPoints <= 2 * qsizetype(width)) {
        qint64 prevT = 0;
        for (qsizetype i = i0; i < i1; ++i) {
            const qint64 t = s.at(i).t;
            if (i == i0 || t - prevT > gapMs)
                newSegment();
            put(i);
            prevT = t;
        }
        return r;
    }

    // 每个像素列一个桶，记录首/最小/最大/尾四个点的下标
    struct Bucket { qsizetype first, last, minI, maxI; float minV, maxV; };
    Bucket b{};
    long long col = LLONG_MIN;
    auto flush = [&] {
        qsizetype idx[4] = {b.first, std::min(b.minI, b.maxI), std::max(b.minI, b.maxI), b.last};
        qsizetype prev = -1;
        for (qsizetype i : idx)
            if (i != prev) {   // 同一个点不重复输出
                put(i);
                prev = i;
            }
    };

    qint64 prevT = 0;
    for (qsizetype i = i0; i < i1; ++i) {
        const TelemetryPoint &p = s.at(i);
        const float v = p.v[size_t(channel)];
        const long long c = (long long)std::floor(xOf(p.t));
        const bool gap = i != i0 && p.t - prevT > gapMs;
        if (gap || c != col) {
            if (col != LLONG_MIN)
                flush();
            if (gap || i == i0)
                newSegment();
            b = {i, i, i, i, v, v};
            col = c;
        } else {
            b.last = i;
            if (v < b.minV) {
                b.minV = v;
                b.minI = i;
            }
            if (v > b.maxV) {
                b.maxV = v;
                b.maxI = i;
            }
        }
        prevT = p.t;
    }
    flush();
    return r;
}
